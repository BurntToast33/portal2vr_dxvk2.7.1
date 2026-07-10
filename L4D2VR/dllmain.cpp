// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <iostream>
#include "game.h"
#include "openvr.h"
#include "vr.h"

DWORD WINAPI InitL4D2VR(HMODULE hModule)
{
    // Make sure -insecure is used
    LPWSTR *szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

    bool insecureEnabled = false; //Change to false to enable -insecure check 
    bool vrEnabled = false;
    int vrDebuglvl = 0;

    for (int i = 0; i < nArgs; ++i)
    {
        if (!wcscmp(szArglist[i], L"-insecure")) insecureEnabled = true;
        else if (!wcscmp(szArglist[i], L"-vr")) vrEnabled = true;
        else if (!wcscmp(szArglist[i], L"-vrdebug"))
        {
            if (i + 1 < nArgs)
            {
                wchar_t* end = nullptr;
                long level = wcstol(szArglist[i + 1], &end, 10);

                if (*end == L'\0')
                {
                    vrDebuglvl = static_cast<int>(level);
                    ++i;
                }
            }
        }
    }
    LocalFree(szArglist);

    if (!insecureEnabled)
    {
        MessageBox(0, "Game is using a modified dll, use -insecure in launch options.", "Portal2 VR", MB_ICONERROR | MB_OK);
        ExitProcess(0);
    }

    if (vrDebuglvl) 
    {
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

        DWORD mode = 0;
        GetConsoleMode(g_hConsole, &mode);
        SetConsoleMode(g_hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    {
        std::lock_guard<std::mutex> lock(g_GameMutex);
        g_Game = new Game();
        g_Game->m_VrEnabled = vrEnabled;
        g_Game->m_VRDebuglvl = vrDebuglvl;
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


