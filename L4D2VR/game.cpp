#include "game.h"
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include "vr.h"
#include "hooks.h"
#include "offsets.h"
#include "sigscanner.h"


static std::mutex logMutex;
using tCreateInterface = void* (__cdecl*)(const char* name, int* returnCode);


// === Utility: Retry module load with logging ===
static HMODULE GetModuleWithRetry(const char* dllname, int maxTries = 500, int delayMs = 50)
{
    for (int i = 0; i < maxTries; ++i)
    {
        HMODULE handle = GetModuleHandleA(dllname);
        if (handle)
            return handle;

        Game::logMsg(LOGTYPE_DEBUG, "Waiting for module to load: %s (attempt %d)", dllname, i + 1);
        Sleep(delayMs);
    }

    Game::errorMsg(("Failed to load module after retrying: " + std::string(dllname)).c_str());
    return nullptr;
}


// === Utility: Safe interface fetch with static cache ===
static void* GetInterfaceSafe(const char* dllname, const char* interfacename)
{
    static std::unordered_map<std::string, void*> cache;

    std::string key = std::string(dllname) + "::" + interfacename;
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    HMODULE mod = GetModuleWithRetry(dllname);
    if (!mod)
        return nullptr;

    auto CreateInterface = reinterpret_cast<tCreateInterface>(GetProcAddress(mod, "CreateInterface"));
    if (!CreateInterface)
    {
        Game::errorMsg(("CreateInterface not found in " + std::string(dllname)).c_str());
        return nullptr;
    }

    int returnCode = 0;
    void* iface = CreateInterface(interfacename, &returnCode);
    if (!iface)
    {
        Game::errorMsg(("Interface not found: " + std::string(interfacename)).c_str());
        return nullptr;
    }

    cache[key] = iface;
    return iface;
}


// === Game Constructor ===
Game::Game() 
{
    clearLog();
}


// === Class Initializer === 
void Game::Initialize() 
{
    GetCurrentDirectory(MAX_STR_LEN, m_GameDir);
    SetVRDlcDisabled(); //Enable / Disable vr assets

#ifndef OVERRIDEVRMODE
    if (!m_VrEnabled)
    {
        logMsg(LOGTYPE_DEBUG, "Game: VR mode disabled.");
        return;
    }
#endif

    //Waiting for dll's to be loaded
    m_BaseClient = reinterpret_cast<uintptr_t>(GetModuleWithRetry("client.dll"));
    m_BaseEngine = reinterpret_cast<uintptr_t>(GetModuleWithRetry("engine.dll"));
    m_BaseMaterialSystem = reinterpret_cast<uintptr_t>(GetModuleWithRetry("MaterialSystem.dll"));
    m_BaseServer = reinterpret_cast<uintptr_t>(GetModuleWithRetry("server.dll"));
    m_BaseVgui2 = reinterpret_cast<uintptr_t>(GetModuleWithRetry("vgui2.dll"));
    m_BaseVguiMatSurface = reinterpret_cast<uintptr_t>(GetModuleWithRetry("vguimatsurface.dll"));

    //Getting interfaces
    m_ClientEntityList = static_cast<IClientEntityList*>(GetInterfaceSafe("client.dll", "VClientEntityList003"));
    m_EnginePanel = static_cast<IEngineVGui*>(GetInterfaceSafe("engine.dll", "VEngineVGui001"));
    m_EngineTrace = static_cast<IEngineTrace*>(GetInterfaceSafe("engine.dll", "EngineTraceClient004"));
    m_EngineClient = static_cast<IEngineClient*>(GetInterfaceSafe("engine.dll", "VEngineClient015"));
    m_MaterialSystem = static_cast<IMaterialSystem*>(GetInterfaceSafe("MaterialSystem.dll", "VMaterialSystem080"));
    m_ModelInfo = static_cast<IModelInfo*>(GetInterfaceSafe("engine.dll", "VModelInfoClient004"));
    m_ModelRender = static_cast<IModelRender*>(GetInterfaceSafe("engine.dll", "VEngineModel016"));
    m_VguiInput = static_cast<IInput*>(GetInterfaceSafe("vgui2.dll", "VGUI_InputInternal001"));
    m_VguiSurface = static_cast<ISurface*>(GetInterfaceSafe("vguimatsurface.dll", "VGUI_Surface031"));
    m_VguiIPanel = static_cast<IPanel*>(GetInterfaceSafe("vgui2.dll", "VGUI_Panel009"));

    m_Offsets = new Offsets();
    LoadCommands();

#ifndef OVERRIDEVRMODE
    m_VR = new VR(this);
    m_VR->CreateHashMaps(); //Need to build hash maps after m_VR is created
#endif

    m_Hooks = new Hooks(this);

    m_Initialized = true;
    logMsg(LOGTYPE_DEBUG, "Game: VR mode initialized successfully.");
}


// === Fallback Interface ===
void *Game::GetInterface(const char *dllname, const char *interfacename)
{
    logMsg(LOGTYPE_DEBUG, "Fallback GetInterface called for %s::%s", dllname, interfacename);
    return GetInterfaceSafe(dllname, interfacename);
}


// === Thread-safe Log Message with Timestamp ===
void Game::logMsg(LOGTYPE logtype, const char* fmt, ...)
{
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    char timebuf[20] = {};
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));

    const char* typeStr;
    switch (logtype) {
        case LOGTYPE_DEBUG: typeStr = "DEBUG"; break;
        case LOGTYPE_WARNING: typeStr = "WARNING"; break;
        case LOGTYPE_ERROR: typeStr = "ERROR"; break;
    }

    printf("[%s][%s] ", timebuf, typeStr);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");

    FILE* file = fopen("vrmod_log.txt", "a");
    if (file)
    {
        fprintf(file, "[%s][%s] ", timebuf, typeStr);
        va_list args2;
        va_start(args2, fmt);
        vfprintf(file, fmt, args2);
        va_end(args2);
        fprintf(file, "\n");
        fclose(file);
    }
}

void Game::clearLog()
{
    std::lock_guard<std::mutex> lock(logMutex);
    FILE* file = fopen("vrmod_log.txt", "w");
    if (file) {
        fclose(file);
    }
}


// === Error Message ===
void Game::errorMsg(const char *msg)
{
    logMsg(LOGTYPE_ERROR, msg);
    MessageBoxA(nullptr, msg, "Portal 2 Error", MB_ICONERROR | MB_OK);
}


// === Entity Access ===
CBaseEntity *Game::GetClientEntity(int entityIndex)
{
    return (CBaseEntity*)(m_ClientEntityList->GetClientEntity(entityIndex)); 
}


// === Player Casts ===
C_BasePlayer* Game::GetPlayer()
{
    return (C_BasePlayer*)GetClientEntity(m_EngineClient->GetLocalPlayer());
}

C_Portal_Player* Game::GetPortalPlayer()
{
    return (C_Portal_Player*)GetClientEntity(m_EngineClient->GetLocalPlayer());
}

C_Portal_Player* Game::GetPortalPlayer(C_BasePlayer* playerEntity)
{
    return (C_Portal_Player*)playerEntity;
}


// === Network Name Utility ===
char *Game::getNetworkName(uintptr_t *entity)
{
    if (!entity)
        return nullptr;

    uintptr_t* vtable = reinterpret_cast<uintptr_t*>(*(entity + 0x8));
    if (!vtable)
        return nullptr;

    uintptr_t* getClientClassFn = reinterpret_cast<uintptr_t*>(*(vtable + 0x8));
    if (!getClientClassFn)
        return nullptr;

    uintptr_t* clientClass = reinterpret_cast<uintptr_t*>(*(getClientClassFn + 0x1));
    if (!clientClass)
        return nullptr;

    char* name = reinterpret_cast<char*>(*(clientClass + 0x8));
    int classID = static_cast<int>(*(clientClass + 0x10));

    logMsg(LOGTYPE_DEBUG, "Network class: ID: %d, Name: %s", classID, name ? name : "nullptr");
    return name;
}


// === Commands ===
void Game::ClientCmd(const char *szCmdString)
{
    if (m_EngineClient)
        m_EngineClient->ClientCmd(szCmdString);
}

void Game::ClientCmd_Unrestricted(const char *szCmdString)
{
    if (m_EngineClient)
        m_EngineClient->ClientCmd_Unrestricted(szCmdString);
}


// === File Parsers ===
void Game::LoadCommands()
{
    std::string path = std::string(m_GameDir) + "/portal2_dlc3/cfg/VR_autoexec.cfg";

    errno = 0;
    std::ifstream file(path);
    if (!file.is_open())
    {
        logMsg(LOGTYPE_WARNING, "Failed to open VR_autoexec.cfg: %s", std::strerror(errno));
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#')
            continue;

        ClientCmd_Unrestricted(line.c_str());
    }

    logMsg(LOGTYPE_DEBUG, "VR_autoexec.cfg loaded");
}

void Game::SetVRDlcDisabled()
{
    std::filesystem::path path = std::string(m_GameDir) + "/portal2_dlc3/dlc_disabled.txt";

#ifdef OVERRIDEVRMODE_ASSETS
    std::error_code ec;
    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path, ec);
        if (ec)
            logMsg(LOGTYPE_WARNING, "Failed to delete dlc_disabled.txt: %s", ec.message().c_str());
        else
            logMsg(LOGTYPE_DEBUG, "Deleted dlc_disabled.txt");
    }
    else
        logMsg(LOGTYPE_DEBUG, "dlc_disabled.txt doesn't exist skipping");
#endif

#ifndef OVERRIDEVRMODE_ASSETS
    if (m_VrEnabled)
    {
        std::error_code ec;
        if (std::filesystem::exists(path))
        {
            std::filesystem::remove(path, ec);
            if (ec)
                logMsg(LOGTYPE_WARNING, "Failed to delete dlc_disabled.txt: %s", ec.message().c_str());
            else
                logMsg(LOGTYPE_DEBUG, "Deleted dlc_disabled.txt");
        }
        else
            logMsg(LOGTYPE_DEBUG, "dlc_disabled.txt doesn't exist skipping");
    }
    else
    {
        if (!std::filesystem::exists(path))
        {
            errno = 0;
            std::ofstream ofs(path);
            if (!ofs)
                logMsg(LOGTYPE_WARNING, "Failed to create dlc_disabled.txt: %s", std::string(std::strerror(errno)));
            
            else
                logMsg(LOGTYPE_DEBUG, "Created dlc_disabled.txt");
        }
        else
            logMsg(LOGTYPE_DEBUG, "dlc_disabled.txt exists skipping");
    }
#endif
}


