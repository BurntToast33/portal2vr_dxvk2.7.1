#include "game.h"
#include "util.h"


int InitDLLMap()
{
	if (!g_Game) {
		std::cout << "g_Game needs to be initialized to get the dll base offset" << std::endl;
		return -1;
	}
    
	DLLMap = {
		{ DLL::CLIENT, &g_Game->m_BaseClient },
		{ DLL::SERVER, &g_Game->m_BaseServer },
		{ DLL::ENGINE, &g_Game->m_BaseEngine },
		{ DLL::MATERIALSYSTEM, &g_Game->m_BaseMaterialSystem },
		{ DLL::VGUI2, &g_Game->m_BaseVgui2 },
		{ DLL::VGUIMATSURFACE, &g_Game->m_BaseVguiMatSurface }
	};

	return 0;
}

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

void FunctionAddress(DLL dllFile, void* fnPtr)
{
    if (DLLMap.empty() && InitDLLMap()) return;

    uintptr_t FnAddress = reinterpret_cast<uintptr_t>(fnPtr);

    if (FnAddress < *DLLMap[dllFile])
    {
        std::cout << "\n    - Function outside dll, skipping" << std::endl;
        return;
    }

    std::cout <<
        "\n    - DLL Offset: 0x" << std::hex << FnAddress - *DLLMap[dllFile] << std::dec <<
        "\n    - Thunk: " << (ResolveThunk(FnAddress) != FnAddress) <<
        "\n    - Executable: " << IsExecutableAddress(FnAddress)
        << std::endl;
}

void ScanVTable(DLL dllFile, void** vtable, size_t length)
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

void TraceCaller(const char* name, uintptr_t caller, DLL dllFile)
{
    if (DLLMap.empty() && InitDLLMap())
        return;

    uintptr_t base = *DLLMap[dllFile];

    std::cout
        << "\n" << name << " called from:\n"
        << "  Address: 0x"
        << std::hex << caller
        << "\n  Offset: 0x"
        << (caller - base)
        << std::dec
        << std::endl;
}