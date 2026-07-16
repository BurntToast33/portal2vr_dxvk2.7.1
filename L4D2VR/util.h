#pragma once
#include <iostream>
#include <unordered_map>
#include <windows.h>
#include "sdk.h"
#include "Zydis.h"

struct CPUContext
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

using MidHookCallback = void(*)(CPUContext*);

struct MidHook
{
    uintptr_t target;          // address patched
    uintptr_t trampoline;      // allocated trampoline
    uintptr_t callback;        // callback address
    uint8_t originalBytes[32];

    size_t stolenSize;         // bytes overwritten
    bool active;
};

uintptr_t ResolveThunk(uintptr_t addr);
bool IsExecutableAddress(uintptr_t addr);
void FunctionAddress(std::string dllFile, void* fnPtr);
void ScanVTable(std::string dllFile, void** vtable, size_t length);
void PrintMemRegion(void* obj, int startIndex, int endIndex, const char* name);
void PrintVGUITree(VPANEL Parent); //Prints everything under the parent
void TraceCaller(const char* name, uintptr_t caller, std::string dllFile);

// Memory utilities
void WriteMemory(void* address, void* data, size_t size);
bool WriteJump(void* src, void* dst);
void* AllocateExecutable(size_t size);

// Mid hook system
void InstallMidHook(void* address,MidHookCallback callback);
/*
    Usage:

    InstallMidHook((void*)(g_Game->m_BaseEngine + 0x1277d0),
	[](CPUContext* ctx)
	{
		printf("ECX = %08X\n", ctx->ecx);
	});
*/

void* CreateMidHookStub(MidHookCallback callback, void* trampoline);

// Internal helpers
size_t GetInstructionLength(uintptr_t address, size_t minimumSize);
void* CreateTrampoline(uintptr_t address, size_t stolenSize);
void MidHookDispatcher(CPUContext* context);
bool HasRelativeOperand(const ZydisDecodedInstruction& instruction);

//maxLength is the the max distance it should scan before hard stopping, if it runs out of exec address it breaks early
template<typename T>
void ScanClassVTable(std::string dllFile, T* instance, size_t maxLength)
{
    if (!instance)
    {
        std::cout << "Util: Invalid class instance" << std::endl;;
        return;
    }

    void** vtable = *reinterpret_cast<void***>(instance);

    if (!vtable)
    {
        std::cout << "Util: Invalid vtable pointer" << std::endl;
        return;
    }

    std::cout << "Util: Scanning vtable for class at " << instance << std::endl;

    size_t count = 0;

    while (count < maxLength)
    {
        void* entry = vtable[count];

        if (!entry)
            break;

        if (!IsExecutableAddress(reinterpret_cast<uintptr_t>(entry)))
            break;

        count++;
    }

    std::cout << "Util: Detected " << count << " entries" << std::endl;
    if (count == maxLength) std::cout << "Util: Warning, detected entries = maxLength" << std::endl;
    ScanVTable(dllFile, vtable, count);
}

//Uses the hex offset to calculate vtable index
template<typename T>
void ScanClassFunction(std::string dllFile, T* instance, size_t offset)
{
    if (!instance)
    {
        std::cout << "Util: Invalid class instance" << std::endl;;
        return;
    }

    void** vtable = *reinterpret_cast<void***>(instance);

    if (!vtable)
    {
        std::cout << "Util: Invalid vtable pointer" << std::endl;
        return;
    }

    size_t index = offset / sizeof(void*);
    std::cout << "Util: VTable[" << index << "]";
    FunctionAddress(dllFile, vtable[index]);
}