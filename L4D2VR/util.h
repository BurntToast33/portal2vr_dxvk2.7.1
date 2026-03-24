#pragma once
#include <iostream>
#include <unordered_map>
#include <windows.h>


enum DLL {
	DLL_CLIENT = 0,
	DLL_SERVER, 
	DLL_ENGINE,
	DLL_MATERIALSYSTEM,
	DLL_VGUI2,
    DLL_VGUIMATSURFACE
};

inline std::unordered_map<DLL, uintptr_t*> DLLMap;
static uintptr_t vguimatsurface;

int InitDLLMap();
uintptr_t ResolveThunk(uintptr_t addr);
bool IsExecutableAddress(uintptr_t addr);
void FunctionAddress(DLL dllFile, void* fnPtr);
void ScanVTable(DLL dllFile, void** vtable, size_t length);

//maxLength is the the max distance it should scan before hard stopping, if it runs out of exec address it breaks early
template<typename T>
void ScanClassVTable(DLL dllFile, T* instance, size_t maxLength)
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