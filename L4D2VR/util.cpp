#include "game.h"
#include "util.h"

static MidHookCallback g_MidHookCallback = nullptr;
static MidHook g_MidHook;

uintptr_t ResolveThunk(uintptr_t addr)
{
    uint8_t* p = reinterpret_cast<uint8_t*>(addr);

    // -------------------------
    // jmp rel32 (x86 + x64)
    // E9 xx xx xx xx
    // -------------------------
    if (p[0] == 0xE9)
    {
        int32_t rel = *reinterpret_cast<int32_t*>(p + 1);
        return addr + 5 + rel;
    }

#if defined(_M_X64) || defined(__x86_64__)

    // -------------------------
    // jmp [rip + imm32] (x64)
    // FF 25 xx xx xx xx
    // -------------------------
    if (p[0] == 0xFF && p[1] == 0x25)
    {
        int32_t ripRel = *reinterpret_cast<int32_t*>(p + 2);
        uintptr_t* target =
            reinterpret_cast<uintptr_t*>(addr + 6 + ripRel);
        return *target;
    }

#elif defined(_M_IX86) || defined(__i386__)

    // -------------------------
    // jmp [imm32] (x86)
    // FF 25 xx xx xx xx
    // -------------------------
    if (p[0] == 0xFF && p[1] == 0x25)
    {
        uintptr_t* target =
            *reinterpret_cast<uintptr_t**>(p + 2);
        return *target;
    }

#endif

    // No thunk detected
    return addr;
}

bool IsExecutableAddress(uintptr_t addr)
{
    MEMORY_BASIC_INFORMATION mbi{};

    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
        return false;

    if (mbi.State != MEM_COMMIT)
        return false;

    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    DWORD protect = mbi.Protect & 0xFF;

    return protect == PAGE_EXECUTE ||
        protect == PAGE_EXECUTE_READ ||
        protect == PAGE_EXECUTE_READWRITE ||
        protect == PAGE_EXECUTE_WRITECOPY;
}

void FunctionAddress(std::string dllFile, void* fnPtr)
{
    uintptr_t Dll;
    for (const auto& [cachedName, handle] : dllList)
    {
        if (cachedName == dllFile)
            Dll = reinterpret_cast<uintptr_t>(handle);
    }

    if (!Dll)
    {
        std::cout << "\n Util: Dll not pre-loaded with GetModuleWithTimeout skipping." << std::endl;
        return;
    }

    uintptr_t FnAddress = reinterpret_cast<uintptr_t>(fnPtr);

    if (FnAddress < Dll)
    {
        std::cout << "\n    - Function outside dll, skipping" << std::endl;
        return;
    }

    std::cout <<
        "\n    - DLL Offset: 0x" << std::hex << FnAddress - Dll << std::dec <<
        "\n    - Thunk: " << (ResolveThunk(FnAddress) != FnAddress) <<
        "\n    - Executable: " << IsExecutableAddress(FnAddress)
        << std::endl;
}

void ScanVTable(std::string dllFile, void** vtable, size_t length)
{
    for (size_t I = 0; I < length; ++I)
    {
        std::cout << "Util: VTable[" << I << "]";
        FunctionAddress(dllFile, vtable[I]);
    }
}

void PrintMemRegion(void* obj, int startIndex, int endIndex, const char* name)
{
    int* fields = static_cast<int*>(obj);

    printf("Util: ==== %s @ %p ====\n", name, obj);

    for (int i = startIndex; i <= endIndex; i++)
    {
        int value = fields[i];

        printf(
            "Util: [0x%02X | +0x%03X] = 0x%08X (%d)\n",
            i,
            i * 4,
            value,
            value
        );
    }

    printf("\n");
}

void PrintVGUITree(VPANEL Panel)
{
    if (!Panel)
        return;

    const char* name = g_Game->m_VguiIPanel->GetName(Panel);
    const char* parent = g_Game->m_VguiIPanel->GetName(g_Game->m_VguiIPanel->GetParent(Panel));
    printf("Parent: %s, ID: %d -> Panel: %s, ID: %d\n", parent, g_Game->m_VguiIPanel->GetParent(Panel), name, Panel);

    for (int I = 0; I < g_Game->m_VguiIPanel->GetChildCount(Panel); I++)
    {
        PrintVGUITree(g_Game->m_VguiIPanel->GetChild(Panel, I));
    }
}

void TraceCaller(const char* name, uintptr_t caller, std::string dllFile)
{
    uintptr_t Dll;
    for (const auto& [cachedName, handle] : dllList)
    {
        if (cachedName == dllFile)
            Dll = reinterpret_cast<uintptr_t>(handle);
    }

    if (!Dll)
    {
        std::cout << "\n Util: Dll not pre-loaded with GetModuleWithTimeout skipping." << std::endl;
        return;
    }

    std::cout
        << "\n" << name << " called from:\n"
        << "  Address: 0x"
        << std::hex << caller
        << "\n  Offset: 0x"
        << (caller - Dll)
        << std::dec
        << std::endl;
}

void WriteMemory(void* address, void* data, size_t size)
{
    if (!address || !data || size == 0)
    {
        std::cout << "Util: Invalid WriteMemory parameters" << std::endl;
        return;
    }

    DWORD oldProtect;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        std::cout << "Util: VirtualProtect unlock failed" << std::endl;
        return;
    }

    memcpy(address, data, size);

    DWORD restoreProtect;
    if (!VirtualProtect(address, size, oldProtect, &restoreProtect))
        std::cout << "Util: VirtualProtect restore failed" << std::endl;

    FlushInstructionCache(GetCurrentProcess(), address, size);
}

bool WriteJump(void* src, void* dst)
{
    uintptr_t source = reinterpret_cast<uintptr_t>(src);
    uintptr_t destination = reinterpret_cast<uintptr_t>(dst);
    int64_t difference = static_cast<int64_t>(destination) - static_cast<int64_t>(source) - 5;

    if (difference < INT32_MIN || difference > INT32_MAX)
    {
        std::cout << "Util: JMP destination out of range" << std::endl;
        return false;
    }

    uint8_t patch[5]{};
    patch[0] = 0xE9;
    int32_t relative = static_cast<int32_t>(difference);

    memcpy(patch + 1, &relative, sizeof(relative));
    WriteMemory(src, patch, sizeof(patch));
    return true;
}

void* AllocateExecutable(size_t size)
{
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

void InstallMidHook(void* address, MidHookCallback callback)
{
    MidHook hook{};
    uintptr_t target = reinterpret_cast<uintptr_t>(address);

    if (!IsExecutableAddress(target))
    {
        std::cout << "Util: Target is not executable" << std::endl;
        return;
    }

    size_t stolen = GetInstructionLength(target, 5);
    if (!stolen)
    {
        std::cout << "Util: Failed calculating stolen bytes" << std::endl;
        return;
    }

    if (stolen > sizeof(hook.originalBytes))
    {
        std::cout << "Util: Too many stolen bytes" << std::endl;
        return;
    }

    void* trampoline = CreateTrampoline(target, stolen);
    if (!trampoline)
    {
        std::cout << "Util: Failed creating trampoline" << std::endl;
        return;
    }

    memcpy(hook.originalBytes, reinterpret_cast<void*>(target), stolen);

    hook.target = target;
    hook.trampoline = reinterpret_cast<uintptr_t>(trampoline);
    hook.callback = reinterpret_cast<uintptr_t>(callback);
    hook.stolenSize = stolen;
    hook.active = true;

    g_MidHookCallback = callback;

    void* stub = CreateMidHookStub(callback, trampoline);
    if (!stub)
    {
        std::cout << "Failed to create MidHookStub" << std::endl;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        hook.active = false;
        return;
    }

    WriteJump(reinterpret_cast<void*>(target), stub);
    g_MidHook = hook;
    return;
}

void* CreateMidHookStub(MidHookCallback callback, void* trampoline)
{
    constexpr size_t stubSize = 32;
    uint8_t* stub = reinterpret_cast<uint8_t*>(AllocateExecutable(stubSize));

    if (!stub)
    {
        std::cout << "Util: Failed allocating mid hook stub" << std::endl;
        return nullptr;
    }

    memset(stub, 0xCC, stubSize);
    uint8_t* code = stub;

    *code++ = 0x60; //push ad
    *code++ = 0x54; //push esp
    *code++ = 0xE8; //call relative callback

    uintptr_t callAddress = reinterpret_cast<uintptr_t>(code);
    int32_t callbackRelative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(MidHookDispatcher) - callAddress - 4);

    memcpy(code, &callbackRelative, sizeof(callbackRelative));

    code += 4;

    //add esp, 4
    *code++ = 0x83;
    *code++ = 0xC4;
    *code++ = 0x04;

    *code++ = 0x61; //pop ad
    *code++ = 0xE9; //jmp trampoline

    uintptr_t jmpAddress = reinterpret_cast<uintptr_t>(code);
    int64_t trampolineRelative = static_cast<int64_t>(reinterpret_cast<uintptr_t>(trampoline)) - 
        static_cast<int64_t>(jmpAddress) - 4;

    if (trampolineRelative < INT32_MIN || trampolineRelative > INT32_MAX)
    {
        std::cout << "Util: Stub trampoline jump out of range" << std::endl;
        VirtualFree(stub, 0, MEM_RELEASE);

        return nullptr;
    }

    int32_t trampolineOffset = static_cast<int32_t>(trampolineRelative);

    memcpy(code, &trampolineOffset, sizeof(trampolineOffset));
    FlushInstructionCache(GetCurrentProcess(), stub, stubSize);
    return stub;
}

void MidHookDispatcher(CPUContext* context)
{
    if (g_MidHookCallback) g_MidHookCallback(context);
}

size_t GetInstructionLength(uintptr_t address, size_t minimumSize)
{
    ZydisDecoder decoder;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32)))
        return 0;

    size_t total = 0;
    while (total < minimumSize)
    {
        if (!IsExecutableAddress(address + total))
        {
            std::cout << "Util: Invalid instruction address" << std::endl;
            return 0;
        }

        ZydisDecodedInstruction instruction{};
        ZyanStatus status = ZydisDecoderDecodeInstruction(&decoder, nullptr, reinterpret_cast<void*>(address + total),
                15, &instruction);

        if (!ZYAN_SUCCESS(status))
        {
            std::cout << "Util: Failed decoding instruction at 0x" << std::hex << address + total << std::dec << std::endl;
            return 0;
        }

        if (HasRelativeOperand(instruction))
        {
            std::cout << "Util: Relative instruction found at 0x" << std::hex << address + total
                << std::dec << ", trampoline relocation required\n";
            return 0;
        }

        total += instruction.length;
    }

    return total;
}

void* CreateTrampoline(uintptr_t address, size_t stolenSize)
{
    size_t trampolineSize = stolenSize + 5;
    uint8_t* trampoline = reinterpret_cast<uint8_t*>(AllocateExecutable(trampolineSize));

    if (!trampoline)
    {
        std::cout << "Util: Failed allocating trampoline\n";
        return nullptr;
    }

    memset(trampoline, 0xCC, trampolineSize);
    memcpy(trampoline, reinterpret_cast<void*>(address), stolenSize);

    uintptr_t returnAddress = address + stolenSize;
    uint8_t* jump = trampoline + stolenSize;
    jump[0] = 0xE9;
    uintptr_t trampolineJump = reinterpret_cast<uintptr_t>(jump);
    int64_t difference = static_cast<int64_t>(returnAddress) - static_cast<int64_t>(trampolineJump) - 5;

    if (difference < INT32_MIN || difference > INT32_MAX)
    {
        std::cout << "Util: Trampoline return jump out of range" << std::endl;;

        VirtualFree(trampoline, 0, MEM_RELEASE);
        return nullptr;
    }

    int32_t relative = static_cast<int32_t>(difference);

    memcpy(jump + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);
    return trampoline;
}

bool HasRelativeOperand(const ZydisDecodedInstruction& instruction)
{
    return (instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;
}