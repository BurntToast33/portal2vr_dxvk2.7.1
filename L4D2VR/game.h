#pragma once
#include <cstdint>
#include <array>
#include "vector.h"
#include <mutex>
#include <condition_variable>
#include <d3d9.h>
#include "d3d9_device.h"

#define MAX_STR_LEN 256
//#define OVERRIDEVRMODE //For testing hooks

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
    LOGTYPE_DEBUG = 0,
    LOGTYPE_WARNING = 1,
    LOGTYPE_ERROR = 2
};


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


    // === Module Base Addresses ===
    uintptr_t m_BaseEngine = 0;
    uintptr_t m_BaseClient = 0;
    uintptr_t m_BaseServer = 0;
    uintptr_t m_BaseMaterialSystem = 0;
    uintptr_t m_BaseVguiMatSurface = 0;
    uintptr_t m_BaseVgui2 = 0;


    // === Internal Systems ===
    Offsets *m_Offsets = nullptr;
    VR *m_VR = nullptr;
    Hooks *m_Hooks = nullptr;


    // === DirectX Device ===
    dxvk::D3D9DeviceEx* m_DxDevice = nullptr;

    Vector m_singlePlayerPortalColors[3] = { Vector(255.0f, 255.0f, 255.0f), Vector(64.0f, 160.0f, 255.0f), Vector(255.0f, 160.0f, 32.0f) };

    bool m_Initialized = false;
    bool m_VrEnabled = false;

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


    // === Command Execution ===
    void ClientCmd(const char* szCmdString);
    void ClientCmd_Unrestricted(const char* szCmdString);


    // === File Parsers
    void LoadCommands();
    void SetVRDlcDisabled();
};


// === Logging Macros (Debug Only) ===
#ifdef _DEBUG
#define LOG(fmt, ...) Game::logMsg("[LOG] " fmt, ##__VA_ARGS__)
#define ERR(msg) Game::errorMsg("[ERROR] " msg)
#else
#define LOG(fmt, ...)
#define ERR(msg)
#endif

