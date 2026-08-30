#include "game.h"
#include <Windows.h>
#include <iostream>
#include <filesystem>
#include "vr.h"
#include "hooks.h"
#include "offsets.h"
#include "sigscanner.h"
#include "PCCM.h"
#include "luamanager.h"

static std::mutex logMutex;

std::string ToLower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

std::string ToLower(const char* input)
{
    return ToLower(std::string(input));
}

// === Game Constructor ===
Game::Game() 
{
    clearLog();
}

// === Class Initializer === 
void Game::Initialize(bool IECheck) 
{
    logMsg(LOGTYPE_DEBUG, "IECheck: %d", IECheck);
    GetCurrentDirectory(MAX_STR_LEN, m_GameDir);

    std::string gameDir = m_GameDir;
    std::string GameType = ToLower(gameDir.erase(0, gameDir.find_last_of("\\/") + 1));
    
    if (!strcmp("portal 2", GameType.c_str())) m_GameType = { GameType, GAMETYPE_PORTAL2 };
    else if (!strcmp("portal reloaded", GameType.c_str())) m_GameType = { GameType, GAMETYPE_PORTAl_RELOADED };
    logMsg(LOGTYPE_DEBUG, "Game: %s, Game type set to %d", m_GameType.first.c_str(), m_GameType.second);

    SetVRDlcDisabled(); //Enable / Disable vr assets

    if (!m_VrEnabled && !m_VRDevMode)
    {
        logMsg(LOGTYPE_DEBUG, "Game: VR mode disabled.");
        return;
    }

    logMsg(LOGTYPE_DEBUG, "Debug level: %d", m_VRDebuglvl);

    //Waiting for dll's to be loaded
    GetModuleWithTimeout(DLL_CLIENT);
    GetModuleWithTimeout(DLL_SERVER);
    GetModuleWithTimeout(DLL_ENGINE);
    GetModuleWithTimeout(DLL_MATERIALSYSTEM);
    GetModuleWithTimeout(DLL_VGUI2);
    GetModuleWithTimeout(DLL_VGUIMATSURFACE);
    //GetModuleWithTimeout(DLL_FILESYSTEM_STDIO);
    HMODULE steamAPI = GetModuleWithTimeout(DLL_STEAMAPI);

    //Getting interfaces
    m_ClientEntityList = static_cast<IClientEntityList*>(GetInterfaceSafe(DLL_CLIENT, "VClientEntityList003"));
    m_EnginePanel = static_cast<IEngineVGui*>(GetInterfaceSafe(DLL_ENGINE, "VEngineVGui001"));
    m_EngineTrace = static_cast<IEngineTrace*>(GetInterfaceSafe(DLL_ENGINE, "EngineTraceClient004"));
    m_EngineClient = static_cast<IEngineClient*>(GetInterfaceSafe(DLL_ENGINE, "VEngineClient015"));
    m_MaterialSystem = static_cast<IMaterialSystem*>(GetInterfaceSafe(DLL_MATERIALSYSTEM, "VMaterialSystem080"));
    m_ModelInfo = static_cast<IModelInfo*>(GetInterfaceSafe(DLL_ENGINE, "VModelInfoClient004"));
    m_ModelRender = static_cast<IModelRender*>(GetInterfaceSafe(DLL_ENGINE, "VEngineModel016"));
    m_VguiInput = static_cast<IInput*>(GetInterfaceSafe(DLL_VGUI2, "VGUI_InputInternal001"));
    m_VguiSurface = static_cast<ISurface*>(GetInterfaceSafe(DLL_VGUIMATSURFACE, "VGUI_Surface031"));
    m_VguiIPanel = static_cast<IPanel*>(GetInterfaceSafe(DLL_VGUI2, "VGUI_Panel009"));
    m_ISteamUser = GetSteamUserInterface(steamAPI);
    m_FileSystem = static_cast<IFileSystem*>(GetInterfaceSafe(DLL_FILESYSTEM_STDIO, "VFileSystem017"));

    m_Offsets = new Offsets(m_GameType.second);
    LoadCommands();

    m_PCCM = new PCCM(this);

    std::string path = std::string(m_GameDir) + "\\VR\\content";
    //m_FileSystem->AddSearchPath(path.c_str(), "mod", PATH_ADD_TO_TAIL);
    //m_FileSystem->AddSearchPath(path.c_str(), "game", PATH_ADD_TO_TAIL);

    //m_FileSystem->PrintSearchPaths();

    if (!m_VRDevMode)
    {
        m_VR = new VR(this);
        m_LuaManager = new LuaManager(this);

        m_VR->CreateHashMaps(); //Need to build hash maps after m_VR and m_PCCM is created
    }

    m_Hooks = new Hooks(this);

    m_Initialized = true;
    logMsg(LOGTYPE_DEBUG, (m_VRDevMode) ? "Game: 2D mode intilized with dev tools." : "Game: VR mode initialized successfully.");
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

    char timebuf[20]{};
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now_c));

    const char* typeStr = "UNKNOWN";
    switch (logtype)
    {
        case LOGTYPE_DEBUG:   typeStr = "DEBUG";   break;
        case LOGTYPE_WARNING: typeStr = "WARNING"; break;
        case LOGTYPE_ERROR:   typeStr = "ERROR";   break;
        case LOGTYPE_INFO:    typeStr = "INFO";    break;
        case LOGTYPE_LUA:     typeStr = "LUA";     break;
    }

    char buffer[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    FILE* file = fopen("vrmod_log.txt", "a");
    if (file)
    {
        fprintf(file, "[%s][%s] %s\n", timebuf, typeStr, buffer);
        fclose(file);
    }

    const char* color = ANSI_RESET;
    switch (logtype)
    {
        case LOGTYPE_DEBUG:   color = ANSI_RESET;  break;
        case LOGTYPE_WARNING: color = ANSI_YELLOW; break;
        case LOGTYPE_ERROR:   color = ANSI_RED;    break;
        case LOGTYPE_INFO:    color = ANSI_GREEN;  break;
        case LOGTYPE_LUA:     color = ANSI_BLUE;   break;
    }

    printf("%s[%s][%s] %s%s\n", color, timebuf, typeStr, buffer, ANSI_RESET);
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
    logMsg(LOGTYPE_ERROR, "%s", msg);
    MessageBoxA(nullptr, msg, "Portal2 VR Error", MB_ICONERROR | MB_OK);
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

    if (m_OverrideVRAssets) 
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

        return;
    }
    
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
}


