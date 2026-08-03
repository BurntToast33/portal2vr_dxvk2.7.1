#pragma once
#include <cstdint>
#include <array>
#include "vector.h"
#include <mutex>
#include <condition_variable>
#include "d3d9_device.h"

#define MAX_STR_LEN 256


#define ANSI_RESET  "\x1b[0m"
#define ANSI_RED    "\x1b[31m"
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_GRAY  "\x1b[90m"

constexpr auto DLL_CLIENT = "client.dll";
constexpr auto DLL_SERVER = "server.dll";
constexpr auto DLL_VGUI2 = "vgui2.dll";
constexpr auto DLL_VGUIMATSURFACE = "vguimatsurface.dll";
constexpr auto DLL_ENGINE = "engine.dll";
constexpr auto DLL_MATERIALSYSTEM = "materialsystem.dll";
constexpr auto DLL_VSTDLIB = "vstdlib.dll";
constexpr auto DLL_STEAMAPI = "steam_api.dll";

class IClientEntityList;
class IEngineVGui;
class IEngineTrace;
class IEngineClient;
class IMaterialSystem;
class IBaseClientDLL;
class IModelInfo;
class IModelRender;
class IMaterial;
class IInput;
class ISurface;
class IPanel;
class CBaseEntity;
class C_BasePlayer;
class C_Portal_Player;
class ISteamUser;
class ICvar;
class PCCM;
struct model_t;


// === Forward Declarations for Internal Systems ===
class Game;
class Offsets;
class VR;
class Hooks;


// === Global Game Instance ===
inline Game *g_Game;


// === Vr toggle sync variables ===
inline std::mutex g_GameMutex;
inline std::condition_variable g_GameCondVar;


// === Console handle ===
static HANDLE g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);


// === Per-Player VR State ===
struct Player
{
    C_BasePlayer* pPlayer = nullptr;
    bool isUsingVR = false;

    Vector controllerPos = { 0.f, 0.f, 0.f };
    QAngle controllerAngle = { 0.f, 0.f, 0.f };
    QAngle prevControllerAngle = { 0.f, 0.f, 0.f };

    bool isMeleeing = false;
    bool isNewSwing = false;
};


// === Log Types ===
enum LOGTYPE
{
    LOGTYPE_DEBUG,
    LOGTYPE_WARNING,
    LOGTYPE_ERROR,
    LOGTYPE_INFO
};

enum GAMETYPE
{
    GAMETYPE_UNKNOWN,
    GAMETYPE_PORTAL2,
    GAMETYPE_PORTAl_RELOADED
};

std::string ToLower(std::string str);

std::string ToLower(const char* input);

static std::vector<std::pair<std::string, HMODULE>> dllList;

class Game
{
public:
    // === Engine Interfaces ===
    IClientEntityList* m_ClientEntityList = nullptr;
    IEngineVGui* m_EnginePanel = nullptr;
    IEngineTrace* m_EngineTrace = nullptr;
    IEngineClient* m_EngineClient = nullptr;
    IMaterialSystem* m_MaterialSystem = nullptr;
    IBaseClientDLL* m_BaseClientDll = nullptr;
    IModelInfo* m_ModelInfo = nullptr;
    IModelRender* m_ModelRender = nullptr;
    IInput* m_VguiInput = nullptr;
    ISurface* m_VguiSurface = nullptr;
    IPanel* m_VguiIPanel = nullptr;
    ISteamUser* m_ISteamUser = nullptr;
    ICvar* m_ICvar = nullptr;

    // === Internal Systems ===
    Offsets *m_Offsets = nullptr;
    VR *m_VR = nullptr;
    Hooks *m_Hooks = nullptr;
    PCCM* m_PCCM = nullptr;


    // === DirectX Device ===
    dxvk::D3D9DeviceEx* m_DxDevice = nullptr;

    Vector m_singlePlayerPortalColors[3] = { Vector(255.0f, 255.0f, 255.0f), Vector(64.0f, 160.0f, 255.0f), Vector(255.0f, 160.0f, 32.0f) };

    bool m_Initialized = false;
    bool m_VrEnabled = false;
    int m_VRDebuglvl = 0;
    bool m_OverrideVRAssets = false;
    bool m_VRDevMode = false;
    GAMETYPE m_GameType = GAMETYPE_UNKNOWN;

    std::array<Player, 24> m_PlayersVRInfo;
    int m_CurrentUsercmdID = -1;

    model_t *m_ArmsModel = nullptr;
    IMaterial *m_ArmsMaterial = nullptr;
    bool m_CachedArmsModel = false;

    char m_GameDir[MAX_STR_LEN];
    int m_WindowWidth = 0, m_WindowHeight = 0;


    // === Constructor ===
    Game();


    // === Class Initializer === 
    void Initialize();


    // === Interface Utilities ===
    void* GetInterface(const char* dllname, const char* interfacename);
    char* getNetworkName(uintptr_t* entity);
    CBaseEntity* GetClientEntity(int entityIndex);
    C_BasePlayer* GetPlayer();
    C_Portal_Player* GetPortalPlayer();
    C_Portal_Player* GetPortalPlayer(C_BasePlayer* playerEntity);


    // === Logging ===
    void clearLog();
    static void logMsg(LOGTYPE logType, const char* fmt, ...);
    static void errorMsg(const char* msg);
    static void SetColorANSI(const char* color);


    // === Command Execution ===
    void ClientCmd(const char* szCmdString);
    void ClientCmd_Unrestricted(const char* szCmdString);


    // === File Parsers
    void LoadCommands();
    void SetVRDlcDisabled();
};

static HMODULE GetModuleWithTimeout(const char* dllname, int timeoutMs = 20000, int pollMs = 50)
{
    std::string name(dllname);
    for (const auto& [cachedName, handle] : dllList)
    {
        if (cachedName == name)
            return handle;
    }

    using namespace std::chrono;
    auto start = steady_clock::now();

    while (true)
    {
        HMODULE handle = GetModuleHandleA(dllname);
        if (handle)
        {
            Game::logMsg(LOGTYPE_DEBUG, "%s took %d ms to load", dllname, (long long)duration_cast<milliseconds>(steady_clock::now() - start).count());
            dllList.push_back(std::pair(name, handle));
            return handle;
        }

        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
        if (elapsed >= timeoutMs)
            break;

        Sleep(pollMs);
    }

    Game::errorMsg(("Failed to load module after timeout: " + std::string(dllname)).c_str());
    return nullptr;
}

static void* GetInterfaceSafe(const char* dllname, const char* interfacename)
{
    using tCreateInterface = void* (__cdecl*)(const char* name, int* returnCode);
    static std::unordered_map<std::string, void*> cache;

    std::string strDllName = std::string(dllname);
    std::string key = strDllName + "::" + interfacename;
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    
    HMODULE mod = nullptr;
    for (const auto& [name, module] : dllList)
    {
        if (name == strDllName)
        {
            mod = module;
            break;
        }
    }

    if (!mod)
        mod = GetModuleWithTimeout(dllname);

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

    if (returnCode) 
    {
        Game::errorMsg(("Interface: " + std::string(interfacename) + " returned with code: " + std::to_string(static_cast<int>(returnCode))).c_str());
        return nullptr;
    }

    cache[key] = iface;
    return iface;
}

static ISteamUser* GetSteamUserInterface(HMODULE steamAPI)
{
    //Path 1: SteamAPI_SteamUser_vXXX exports
    {
        using tSteamUser = ISteamUser * (__cdecl*)();
        const char* versions[] =
        {
            "SteamAPI_SteamUser_v023",
            "SteamAPI_SteamUser_v022",
            "SteamAPI_SteamUser_v021",
            "SteamAPI_SteamUser_v020",
            "SteamAPI_SteamUser_v019"
        };

        for (const char* version : versions)
        {
            auto oSteamUser = reinterpret_cast<tSteamUser>(GetProcAddress(steamAPI, version));

            if (!oSteamUser)
                continue;

            ISteamUser* steamUser = oSteamUser();
            if (steamUser)
                return steamUser;
        }
    }

    //Path 2: SteamInternal_CreateInterface -> ISteamClient -> GetISteamUser
    {
        using tSteamInternal_CreateInterface = void* (__cdecl*)(const char*);
        auto SteamInternal_CreateInterface =
            reinterpret_cast<tSteamInternal_CreateInterface>(GetProcAddress(steamAPI, "SteamInternal_CreateInterface"));

        auto GetHSteamUser =
            reinterpret_cast<int(__cdecl*)()>(GetProcAddress(steamAPI, "SteamAPI_GetHSteamUser"));

        auto GetHSteamPipe =
            reinterpret_cast<int(__cdecl*)()>(
                GetProcAddress(steamAPI,"SteamAPI_GetHSteamPipe"));

        if (SteamInternal_CreateInterface && GetHSteamUser && GetHSteamPipe)
        {
            void* steamClient = nullptr;
            const char* clientVersions[] =
            {
                "SteamClient020",
                "SteamClient019",
                "SteamClient018",
                "SteamClient017"
            };

            for (const char* version : clientVersions)
            {
                steamClient = SteamInternal_CreateInterface(version);
                if (steamClient)
                    break;
            }

            if (steamClient)
            {
                using tGetISteamUser = ISteamUser * (__thiscall*)(void*, int, int, const char*);
                uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(steamClient);
                auto GetISteamUser = reinterpret_cast<tGetISteamUser>(vtable[5]);

                int hUser = GetHSteamUser();
                int hPipe = GetHSteamPipe();

                const char* userVersions[] =
                {
                    "SteamUser023",
                    "SteamUser022",
                    "SteamUser021",
                    "SteamUser020",
                    "SteamUser019"
                };

                for (const char* version : userVersions)
                {
                    ISteamUser* steamUser = GetISteamUser(steamClient, hUser, hPipe, version);
                    if (steamUser) return steamUser;
                }
            }
        }
    }

    Game::logMsg(LOGTYPE_WARNING, "Failed to find SteamUser interface, disabling 3D backgrounds.");
    return nullptr;
}


// === Logging Macros (Debug Only) ===
#ifdef _DEBUG
#define LOG(fmt, ...) Game::logMsg("[LOG] " fmt, ##__VA_ARGS__)
#define ERR(msg) Game::errorMsg("[ERROR] " msg)
#else
#define LOG(fmt, ...)
#define ERR(msg)
#endif

