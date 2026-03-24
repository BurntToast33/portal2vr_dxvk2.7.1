// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <iostream>
#include "game.h"
#include "openvr.h"
#include "vr.h"

// Release if buggy, so we'll be releasing the debug binary (as of 2025 the debug binary crashes on map load)

DWORD WINAPI InitL4D2VR(HMODULE hModule)
{
    // Make sure -insecure is used
    LPWSTR *szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

    bool insecureEnabled = false; //Change to false to enable -insecure check 
    bool vrEnabled = false;
    bool console = false;

    for (int i = 0; i < nArgs; ++i)
    {
        if (!wcscmp(szArglist[i], L"-insecure")) insecureEnabled = true;
        else if (!wcscmp(szArglist[i], L"-vr")) vrEnabled = true;
        else if (!wcscmp(szArglist[i], L"-vrdebug")) console = true;
    }
    LocalFree(szArglist);

    if (!insecureEnabled)
    {
        MessageBox(0, "Game is using a modified dll, use -insecure in launch options.", "Portal2 VR", MB_ICONERROR | MB_OK);
        ExitProcess(0);
    }

    if (console) 
    {
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
    }

    {
        std::lock_guard<std::mutex> lock(g_GameMutex);
        g_Game = new Game();
        g_Game->m_VrEnabled = vrEnabled;
    }
    g_GameCondVar.notify_all();
    g_Game->Initialize();
    return 0;
}



BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitL4D2VR, hModule, 0, NULL);
            break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


