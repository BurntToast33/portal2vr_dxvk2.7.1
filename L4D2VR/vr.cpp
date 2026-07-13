#include "vr.h"
#include <Windows.h>
#include "sdk.h"
#include "game.h"
#include "hooks.h"
#include "trace.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <thread>
#include <type_traits>
#include <algorithm>
#include <d3d9_vr.h>

//Toggles
//#define GetControllerTipMatrix_HeapBuffer //Enable this for cpu's with 32kb or less of L1 cache


VR::VR(Game *game) 
{
    m_Game = game;
    char errorString[MAX_STR_LEN];

    if (!vr::VRSystem())
    {
        vr::HmdError error = vr::VRInitError_None;
        m_System = vr::VR_Init(&error, vr::VRApplication_Scene);

        if (error != vr::VRInitError_None)
        {
            snprintf(errorString, MAX_STR_LEN, "VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
            Game::errorMsg(errorString);
            return;
        }
    }
    else
        m_System = vr::VRSystem();

    if (!vr::VRCompositor())
    {
        Game::errorMsg("Compositor initialization failed.");
        return;
    }

    m_Input = vr::VRInput();
    m_System = vr::OpenVRInternal_ModuleContext().VRSystem();

    m_System->GetRecommendedRenderTargetSize(&m_RenderWidth, &m_RenderHeight);

    if (m_Game->m_VRDebuglvl) 
        m_Game->logMsg(LOGTYPE_DEBUG, "Height: %d, Width: %d", m_Game->m_WindowHeight, m_Game->m_WindowWidth);

    m_WScaleDownRatio = (float)m_Game->m_WindowWidth / m_RenderWidth;
    m_HScaleDownRatio = (float)m_Game->m_WindowHeight / m_RenderHeight;
    m_WScaleUpRatio = (float)m_RenderWidth / m_Game->m_WindowWidth;
    m_HScaleUpRatio = (float)m_RenderHeight / m_Game->m_WindowHeight;

    float l_left = 0.0f, l_right = 0.0f, l_top = 0.0f, l_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);

    float r_left = 0.0f, r_right = 0.0f, r_top = 0.0f, r_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Right, &r_left, &r_right, &r_top, &r_bottom);

    float tanHalfFov[2];

    tanHalfFov[0] = std::max({ -l_left, l_right, -r_left, r_right });
    tanHalfFov[1] = std::max({ -l_top, l_bottom, -r_top, r_bottom });

    m_TextureBounds[0].uMin = 0.5f + 0.5f * l_left / tanHalfFov[0];
    m_TextureBounds[0].uMax = 0.5f + 0.5f * l_right / tanHalfFov[0];
    m_TextureBounds[0].vMin = 0.5f - 0.5f * l_bottom / tanHalfFov[1];
    m_TextureBounds[0].vMax = 0.5f - 0.5f * l_top / tanHalfFov[1];

    m_TextureBounds[1].uMin = 0.5f + 0.5f * r_left / tanHalfFov[0];
    m_TextureBounds[1].uMax = 0.5f + 0.5f * r_right / tanHalfFov[0];
    m_TextureBounds[1].vMin = 0.5f - 0.5f * r_bottom / tanHalfFov[1];
    m_TextureBounds[1].vMax = 0.5f - 0.5f * r_top / tanHalfFov[1];

    m_Aspect = tanHalfFov[0] / tanHalfFov[1];
    m_Fov = 2.0f * atan(tanHalfFov[0]) * 360 / (3.14159265358979323846 * 2);

    InstallApplicationManifest("manifest.vrmanifest");
    SetActionManifest("action_manifest.json");

    std::thread configParser(&VR::WaitForConfigUpdate, this);
    configParser.detach();

    while (!g_D3DVR9) 
        Sleep(10);

    g_D3DVR9->GetBackBufferData(&m_BackBuffer);
    m_Overlay = vr::VROverlay();

    m_Overlay->CreateOverlay("MenuOverlayKey", "MenuOverlay", &m_MainOverlay.m_Handle);
    m_MainOverlay.SetOverlayInputMethod(vr::VROverlayInputMethod_Mouse);
    m_Overlay->SetOverlayFlag(m_MainOverlay.m_Handle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

    const vr::HmdVector2_t mouseScaleMenu = {m_RenderWidth, m_RenderHeight};
    m_Overlay->SetOverlayCurvature(m_MainOverlay.m_Handle, 0.15f);
    m_Overlay->SetOverlayMouseScale(m_MainOverlay.m_Handle, &mouseScaleMenu);

    vr::VRTextureBounds_t bounds{ 0, 0, 1, 1 };
    m_Overlay->SetOverlayTextureBounds(m_MainOverlay.m_Handle, &bounds);
    m_Overlay->SetOverlayTexelAspect(m_MainOverlay.m_Handle, 1.0f);
    m_Overlay->SetOverlayWidthInMeters(m_MainOverlay.m_Handle, 1.5f * (1.0f / m_HScaleDownRatio));

    UpdatePosesAndActions();

    if (m_Game->m_GameType == GAMETYPE_PORTAL2) m_SteamID = GetSteamID64();
    m_IsInitialized = true;
    m_IsVREnabled = true;
}

VR::~VR()
{
    m_IsInitialized = false;
    m_IsVREnabled = false;

    m_BackBuffer.Release();
    m_LeftEye.Release();
    m_RightEye.Release();
    m_MenuTexture.Release();
    m_HudTexture.Release();
}

//Creates the hash maps used to later
void VR::CreateHashMaps()
{
    //Using msaa surface as a lower res render target instead of a msaa surface
    m_MenuTexture.m_OverrideMSAASurface.emplace(m_RenderWidth, m_RenderHeight);


    //Background mappings
    char path[MAX_STR_LEN]{};
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\backgrounds.json", m_Game->m_GameDir);

    if (!LoadStringMap(path, "backgroundMappings", m_BackgroundMapping))
        m_Game->logMsg(LOGTYPE_WARNING, "Failed to parse: %s%s", path, "\nDisabling 3D backgrounds");
    
    m_Game->logMsg(static_cast<LOGTYPE>(!m_BackgroundMapping.size()), "Loaded %zu background mappings.", m_BackgroundMapping.size());


    //UI stuff from here
    OverridePanelLayout("options.res", { "resource/ui/vr_options.res" });
    OverridePanelLayout("KeyboardMouse.res", { "resource/ui/vr_controllersettings.res", [this](std::string& LayoutPath)
    {
        if (m_OverrideControllerUI)
        {
            m_OverrideControllerUI = false;
            return true;
        }

        LayoutPath = "resource/ui/vr_settings.res";
        return true;
    }});

    RegisterPanelCommandListener({ "VRController" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        message->SetString("command", "KeyboardMouse");
        m_OverrideControllerUI = true;
        return false;
    });
    RegisterPanelCommandListener({ "ExitToMainMenu" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        m_LevelExitFix = true;
        return false;
    });
    RegisterPanelCommandListener({ "Btn0" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        if (m_LevelExitFix)
        {
            m_Game->m_EngineClient->ClientCmd_Unrestricted("disconnect");
            m_LevelExitFix = false;
        }

        return false;
    });
    
    //VR Settings layout
    ModifyPanelSettings("SldVRScale", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        settingData["ready"] = 2;
        settingData["min"] = inResourceData->GetFloat("minValue", 0);
        settingData["max"] = inResourceData->GetFloat("maxValue", 0);

        return false;
    });
    m_SlideRead["SldVRScale"] = [this](Panel* panel, float Percentage)
    {
        auto it = m_PanelSettings.find("SldVRScale");
        if (it == m_PanelSettings.end())
            return;

        float min = it->second.GetData<float>("min");
        float max = it->second.GetData<float>("max");
        int ready = it->second.GetData<int>("ready");

        if (ready)
        {
            reinterpret_cast<SliderControl*>(panel)->m_curValue = MinMaxInverse(m_VRScale, min, max);
            it->second.m_Data["ready"] = --ready;
            return;
        }

        WriteConfigEntry("VRScale", min + (max - min) * Percentage);
    };

    ModifyPanelSettings("SldIPDScale", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        settingData["ready"] = 2;
        settingData["min"] = inResourceData->GetFloat("minValue", 0);
        settingData["max"] = inResourceData->GetFloat("maxValue", 0);

        return false;
    });
    m_SlideRead["SldIPDScale"] = [this](Panel* panel, float Percentage)
    {
        auto it = m_PanelSettings.find("SldIPDScale");
        if (it == m_PanelSettings.end())
            return;

        float min = it->second.GetData<float>("min");
        float max = it->second.GetData<float>("max");
        int ready = it->second.GetData<int>("ready");

        if (ready)
        {
            reinterpret_cast<SliderControl*>(panel)->m_curValue = MinMaxInverse(m_IpdScale, min, max);
            it->second.m_Data["ready"] = --ready;
            return;
        }

        WriteConfigEntry("IPDScale", min + (max - min) * Percentage);
    };

    ModifyPanelSettings("DrpRenderWindow", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_RenderWindow;
        return false;
    });
    RegisterPanelCommandListener({ "VRRenderWindow0", "VRRenderWindow1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        bool con = !strcmp(cmd, "VRRenderWindow1");
        m_Game->logMsg(LOGTYPE_DEBUG, "Render window set to ",
            con ? "'true'" : "'false'", " via in game menu.");

        WriteConfigEntry("RenderWindow", con);
        return false;
    });

    ModifyPanelSettings("Drp3DBackground", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_3DMenu;
        return false;
    });
    RegisterPanelCommandListener({ "VR3DBackground0", "VR3DBackground1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        bool con = !strcmp(cmd, "VR3DBackground1");
        m_Game->logMsg(LOGTYPE_DEBUG, "3D menu set to ",
            con ? "'true'" : "'false'", " via in game menu.");

        WriteConfigEntry("Enable3DBackground", con);
        return false;
    });

    ModifyPanelSettings("DrpExperimentalOptimizations", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_ExperimentalOptimizations;
        return false;
    });
    RegisterPanelCommandListener({ "VRExperimentalOptimizations0", "VRExperimentalOptimizations1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        int con = (strcmp(cmd, "VR3DBackground1") == 0);
        m_Game->logMsg(LOGTYPE_DEBUG, "Experimental optimizations set to ",
            con ? "'1'" : "'0'", " via in game menu.");

        WriteConfigEntry("ExperimentalOptimizations", con);
        return false;
    });

    ModifyPanelSettings("DrpSmoothRotation", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_SmoothRotation;
        return false;
    });
    RegisterPanelCommandListener({ "VRSmoothRotation0", "VRSmoothRotation1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        bool con = !strcmp(cmd, "VRSmoothRotation1");
        m_Game->logMsg(LOGTYPE_DEBUG, "Smooth rotation set to ",
            con ? "'true'" : "'false'", " via in game menu.");

        WriteConfigEntry("SmoothRotation", con);
        return false;
    });

    //VR Controller Settings layout
    RegisterPanelCommandListener({ "VRBindings" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        vr::EVRInputError err = m_Input->OpenBindingUI(
            "steam.app.620",
            vr::k_ulInvalidInputValueHandle,
            vr::k_ulInvalidInputValueHandle,
            false
        );

        if (err != vr::VRInputError_None)
            m_Game->logMsg(LOGTYPE_WARNING, "Error opening binding ui: %s", EVRInputErrorToString(err));
        else
            m_Game->logMsg(LOGTYPE_DEBUG, "Opened binding ui");

        return false;
    });

    ModifyPanelSettings("SldTurnSpeed", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        settingData["ready"] = 2;
        settingData["min"] = inResourceData->GetFloat("minValue", 0);
        settingData["max"] = inResourceData->GetFloat("maxValue", 0);
        return false;
    });
    m_SlideRead["SldTurnSpeed"] = [this](Panel* panel, float Percentage)
    {
        auto it = m_PanelSettings.find("SldTurnSpeed");
        if (it == m_PanelSettings.end())
            return;

        float min = it->second.GetData<float>("min");
        float max = it->second.GetData<float>("max");
        int ready = it->second.GetData<int>("ready");

        if (ready)
        {
            reinterpret_cast<SliderControl*>(panel)->m_curValue = MinMaxInverse(m_TurnSpeed, min, max);
            it->second.m_Data["ready"] = --ready;
            return;
        }

        WriteConfigEntry("TurnSpeed", min + (max - min) * Percentage);
    };

    ModifyPanelSettings("DrpSnapTurning", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_SnapTurning;
        return false;
    });
    RegisterPanelCommandListener({ "VRSnapTurning0", "VRSnapTurning1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        bool con = !strcmp(cmd, "VRSnapTurning1");
        m_Game->logMsg(LOGTYPE_DEBUG, "Snap turning set to ",
            con ? "'true'" : "'false'", " via in game menu.");

        WriteConfigEntry("SnapTurning", con);
        return false;
    });

    ModifyPanelSettings("DrpLeftHanded", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_LeftHanded;
        return false;
    });
    RegisterPanelCommandListener({ "VRLeftHanded0", "VRLeftHanded1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        bool con = !strcmp(cmd, "VRLeftHanded1");
        m_Game->logMsg(LOGTYPE_DEBUG, "Left handed set to ",
            con ? "'true'" : "'false'", " via in game menu.");

        WriteConfigEntry("LeftHanded", con);
        return false;
    });

    ModifyPanelSettings("Drp6DOF", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_6DOF;
        return false;
    });
    RegisterPanelCommandListener({ "VR6DOF0", "VR6DOF1" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        bool con = !strcmp(cmd, "VR6DOF1");
        m_Game->logMsg(LOGTYPE_DEBUG, "6DOF set to ",
            con ? "'true'" : "'false'", " via in game menu.");

        WriteConfigEntry("6DOF", con);
        return false;
    });

    ModifyPanelSettings("DrpAimMode", [this](Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)
    {
        reinterpret_cast<HybridButton*>(panel)->m_SetListIndex = m_AimMode;
        return false;
    });
    RegisterPanelCommandListener({ "VRAimMode0", "VRAimMode1", "VRAimMode2" }, [this](const char* cmd, Panel* panel, KeyValues* message)
    {
        int con = !strcmp(cmd, "VRAimMode0") ? 0 : !strcmp(cmd, "VRAimMode1") ? 1 : 2;
        m_Game->logMsg(LOGTYPE_DEBUG, "Aim mode set to '%d' via in game menu.", con);

        WriteConfigEntry("AimMode", con);
        return false;
    });
}

//Binds actions to variables
int VR::SetActionManifest(const char *fileName) 
{
    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\SteamVRActionManifest\\%s", m_Game->m_GameDir, fileName);

    if (m_Input->SetActionManifestPath(path) != vr::VRInputError_None) 
        Game::errorMsg("SetActionManifestPath failed");

    //SetBinding("/actions/main/in/ActivateVR", );
    SetBinding("/actions/main/in/Jump", VRBindingType_Input, "+jump", "-jump");
    SetBinding("/actions/main/in/PrimaryAttack", VRBindingType_Input, "+attack", "-attack");
    SetBinding("/actions/main/in/Reload", VRBindingType_Input, "+reload", "-reload");
    SetBinding("/actions/main/in/Use", VRBindingType_Input, "+use", "-use");
    SetBinding("/actions/main/in/SecondaryAttack", VRBindingType_Input, "+attack2", "-attack2");
    SetBinding("/actions/main/in/NextItem", VRBindingType_Input, "invnext");
    SetBinding("/actions/main/in/PrevItem", VRBindingType_Input, "invprev");
    SetBinding("/actions/main/in/ResetPosition", VRBindingType_Input, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { ResetPosition(); });
    SetBinding("/actions/main/in/Crouch", VRBindingType_Input, "+duck", "-duck", true);
    SetBinding("/actions/main/in/Flashlight", VRBindingType_Input, "impulse 100");
    SetBinding("/actions/main/in/MenuSelect", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_RETURN); });
    SetBinding("/actions/main/in/MenuBack", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_ESCAPE); });
    SetBinding("/actions/main/in/MenuUp", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_UP); });
    SetBinding("/actions/main/in/MenuDown", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_DOWN); });
    SetBinding("/actions/main/in/MenuLeft", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_LEFT); });
    SetBinding("/actions/main/in/MenuRight", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_RIGHT); });
    SetBinding("/actions/main/in/Spray", VRBindingType_Input, "impulse 201");
    SetBinding("/actions/main/in/Scoreboard", VRBindingType_Input, "+showscores", "-showscores", true);
    //SetBinding("/actions/main/in/ShowHUD");
    SetBinding("/actions/main/in/Pause", VRBindingType_Menu, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle) { SendButton(VK_ESCAPE); });

    if (g_Game->m_GameType == GAMETYPE_PORTAl_RELOADED) SetBinding("/actions/main/in/ThirdAttack", VRBindingType_Input, "att3");

    SetBinding("/actions/main/in/Pause", VRBindingType_Input, "gameui_activate", nullptr, false, [this](vr::VRActionHandle_t handle)
    { 
        if (m_Game->m_EngineClient->IsInGame())
            RepositionOverlay(m_MainOverlay.m_Handle, vr::k_unTrackedDeviceIndex_Hmd, OverlayRel_DeviceSpaceForward, { -0.10f, 0.0f, 3.0f }, { RotFlag_UseYaw }); 
    });
    SetBinding("/actions/main/in/Turn", VRBindingType_Analog, nullptr, nullptr, false, [this](vr::VRActionHandle_t handle)
    {
        vr::InputAnalogActionData_t analogActionData;
        if (GetAnalogActionData(handle, analogActionData))
        {
            if (m_SnapTurning)
            {
                if (!m_PressedTurn && analogActionData.x > 0.5)
                {
                    m_RotationOffset.y -= m_SnapTurnAngle;
                    m_PressedTurn = true;
                }
                else if (!m_PressedTurn && analogActionData.x < -0.5)
                {
                    m_RotationOffset.y += m_SnapTurnAngle;
                    m_PressedTurn = true;
                }
                else if (analogActionData.x < 0.3 && analogActionData.x > -0.3)
                    m_PressedTurn = false;
            }
            // Smooth turning
            else
            {
                typedef std::chrono::duration<float, std::milli> duration;
                auto currentTime = std::chrono::steady_clock::now();
                duration elapsed = currentTime - m_PrevFrameTime;
                float deltaTime = elapsed.count();
                m_PrevFrameTime = currentTime;

                float deadzone = 0.2;
                // smoother turning
                float xNormalized = (abs(analogActionData.x) - deadzone) / (1 - deadzone);
                if (analogActionData.x > deadzone)
                {
                    m_RotationOffset.y -= m_TurnSpeed * deltaTime * xNormalized;
                }
                if (analogActionData.x < -deadzone)
                {
                    m_RotationOffset.y += m_TurnSpeed * deltaTime * xNormalized;
                }
            }

            // Wrap from 0 to 360
            m_RotationOffset.y -= 360 * std::floor(m_RotationOffset.y / 360);
        }
    });
    m_Input->GetActionHandle("/actions/main/in/Walk", &m_ActionWalk); //Due to being in a hook I can't use SetBinding

    m_Input->GetActionSetHandle("/actions/main", &m_ActionSet);
    m_ActiveActionSet = {};
    m_ActiveActionSet.ulActionSet = m_ActionSet;

    return 0;
}

void VR::InstallApplicationManifest(const char *fileName)
{
    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\%s", m_Game->m_GameDir, fileName);

    vr::VRApplications()->AddApplicationManifest(path);
}

void VR::PreUpdate()
{
    //Scaling Menu textures
    if (ShouldCapture(Capture_MenuUI) || ShouldCapture(Capture_HudUI))
    {
        static RECT Rect = { 0, 0, m_Game->m_WindowWidth, m_Game->m_WindowHeight };
        m_Game->m_DxDevice->StretchRect(
            m_MenuTexture.m_MSAASurface, &Rect,
            m_MenuTexture.m_Surface, nullptr,
            D3DTEXF_LINEAR
        );
    }

    //Compositing the rendered frame to the back buffer
    if (m_RenderWindow && m_Game->m_EngineClient->IsInGame())
    {
        IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
        rndrContext->SetRenderTarget(NULL);
        rndrContext->Release();

        m_Game->m_DxDevice->StretchRect(
            m_LeftEye.m_Surface, nullptr,
            m_BackBuffer.m_Surface, nullptr,
            D3DTEXF_NONE
        );

        g_D3DVR9->RenderTextureToRenderTargetWithAlpha(m_MenuTexture.m_Texture, m_RenderWidth, m_RenderHeight); 
    }
}

void VR::PostUpdate()
{
    if (!m_IsInitialized || !m_Game->m_Initialized)
        return;

    if (!m_IsVREnabled && !g_D3DVR9)
        return;

    // Prevents crashing at menu
    if (!m_Game->m_EngineClient->IsInGame())
    {
        m_IsCredits = false; //Reset once out of level
        m_Game->m_CachedArmsModel = false;

        if (m_3DMenu && !m_3DMenuLoading && !m_IsLevelBackground && m_CreatedVRTextures && 
            !m_Game->m_EngineClient->IsDrawingLoadingImage() && !m_Game->m_EngineClient->IsInGame())
        {
            std::thread([this]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                Load3DMenu();
            }).detach();

            m_3DMenuLoading = true;
        }
    }
      
    SubmitVRTextures();
    UpdateTracking();
    UpdatePosesAndActions();
    
    if (m_Game->m_VguiSurface->IsCursorVisible()) {
        ProcessMenuInput();
    } else {
        ProcessInput();
    }

    //Clearing menu ui for next frame
    if (ShouldCapture(Capture_MenuUI) || ShouldCapture(Capture_HudUI)) 
    {
        IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
        rndrContext->SetRenderTarget(m_MenuTexture.m_MSAAITex);
        m_Game->m_DxDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
        rndrContext->SetRenderTarget(NULL);
        rndrContext->Release();
    }

    //Pulling ui state
    vr::VREvent_t event;
    while (m_System->PollNextEvent(&event, sizeof(event)))
    {
        switch (event.eventType) 
        {
            case vr::VREvent_DashboardActivated:
            {
                m_MainOverlay.SetOverlayInputMethod(vr::VROverlayInputMethod_None, true);
                break;
            }
            case vr::VREvent_DashboardDeactivated:
            {
                m_MainOverlay.SetOverlayInputMethod(m_MainOverlay.m_SaveStateMethod);
                break;
            }
        }
    }
}

void VR::FirstFrameUpdate()
{
    //Crashes in debug mode for some reason
    //m_CreatedVRTextures = false; //Apparently need to recreate textures or workshop maps don't render properly
    
    m_Game->ClientCmd_Unrestricted("mat_motion_blur_enabled 0");
     
    if (m_Game->m_EngineClient->IsPaused())
        m_Game->ClientCmd_Unrestricted("pause");

    ResetPosition();
    m_3DMenuLoading = false;
}

//Creates the target textures on the engine side to get picked up on dxvk side
void VR::CreateVRTextures()
{
    m_Game->logMsg(LOGTYPE_DEBUG, "RenderTexture - Width: %d, Height: %d", m_RenderWidth, m_RenderHeight);

    m_Game->m_MaterialSystem->isGameRunning = false;
    m_Game->m_MaterialSystem->BeginRenderTargetAllocation();
    m_Game->m_MaterialSystem->isGameRunning = true;

    //m_LeftEye.m_UseMSAA = m_AntiAliasing;
    //m_RightEye.m_UseMSAA = m_AntiAliasing;

    ImageFormat format = m_Game->m_MaterialSystem->GetBackBufferFormat();
    CreateRT(&m_BlankTexture, "blankTexture", 512, 512, RT_SIZE_NO_CHANGE, format);
    CreateRT(&m_LeftEye, "leftEye", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, format);
    CreateRT(&m_RightEye, "rightEye", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, format);
    CreateRT(&m_MenuTexture, "menuTex", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, format);
    CreateRT(&m_HudTexture, "hudTex", m_Game->m_WindowWidth, m_Game->m_WindowHeight, RT_SIZE_NO_CHANGE, format);

    m_Game->m_MaterialSystem->EndRenderTargetAllocation();
    m_CreatedVRTextures = true;
    m_BuiltCaptureMap = false;
}

//Submits vr textures and handles menus
void VR::SubmitVRTextures()
{
    vr::EVRCompositorError leftEye = vr::VRCompositorError_None, rightEye = vr::VRCompositorError_None;

    if (!m_CreatedVRTextures)
        CreateVRTextures();

    //2D mode
    if (ShouldCapture(Capture_2D))
    {
        if (!m_MainOverlay.m_Visible || m_MainOverlay.m_StateFlag != 1)
        {
            RepositionOverlay(m_MainOverlay.m_Handle, vr::k_unTrackedDeviceIndex_Hmd, OverlayRel_WorldSpace, { -0.10f, 1.25f, 3.0f });
            m_MainOverlay.SetOverlayInputMethod(vr::VROverlayInputMethod_Mouse);
            m_MainOverlay.m_StateFlag = 1;
            if (m_Game->m_VRDebuglvl) m_Game->logMsg(LOGTYPE_DEBUG, "2D mode");
        }
        
        m_Overlay->SetOverlayTexture(m_MainOverlay.m_Handle, &m_BackBuffer.m_VRTexture);
        m_MainOverlay.ShowOverlay();

        leftEye = vr::VRCompositor()->Submit(vr::Eye_Left, &m_BlankTexture.m_VRTexture, nullptr, vr::Submit_Default);
        rightEye = vr::VRCompositor()->Submit(vr::Eye_Right, &m_BlankTexture.m_VRTexture, nullptr, vr::Submit_Default);

        if (m_Game->m_VRDebuglvl > 1)
        {
            if (leftEye != vr::VRCompositorError_None && leftEye != m_LastLeftEyeError)
            {
                m_Game->logMsg(LOGTYPE_ERROR, "2D mode left eye error: %s", CompositorErrorToString(leftEye));
                m_LastLeftEyeError = leftEye;
            }

            if (rightEye != vr::VRCompositorError_None && rightEye != m_LastRightEyeError)
            {
                m_Game->logMsg(LOGTYPE_ERROR, "2D mode right eye error: %s", CompositorErrorToString(rightEye));
                m_LastRightEyeError = rightEye;
            }
        }
        return;
    }

    //3D mode
    else if (ShouldCapture(Capture_MenuUI))
    {
        if (!m_MainOverlay.m_Visible || m_MainOverlay.m_StateFlag != 2)
        {
            //This forces forward spawn on background levels
            if (!m_IsLevelBackground)
                RepositionOverlay(m_MainOverlay.m_Handle, vr::k_unTrackedDeviceIndex_Hmd, OverlayRel_DeviceSpaceForward, { -0.10f, 0.0f, 3.0f }, { RotFlag_UseYaw });
            
            else
                RepositionOverlay(m_MainOverlay.m_Handle, vr::k_unTrackedDeviceIndex_Hmd, OverlayRel_WorldSpace, { -0.10f, 1.25f, 3.0f });

            m_MainOverlay.SetOverlayInputMethod(vr::VROverlayInputMethod_Mouse);
            m_MainOverlay.m_StateFlag = 2;
            if (m_Game->m_VRDebuglvl) m_Game->logMsg(LOGTYPE_DEBUG, "3D mode, Menu state");
        }

        m_Overlay->SetOverlayTexture(m_MainOverlay.m_Handle, &m_MenuTexture.m_VRTexture);
        m_MainOverlay.ShowOverlay();
    }

    else if (ShouldCapture(Capture_HudUI))
    {
        if (!m_MainOverlay.m_Visible || m_MainOverlay.m_StateFlag != 3)
        {
            RepositionOverlay(m_MainOverlay.m_Handle, vr::k_unTrackedDeviceIndex_Hmd, OverlayRel_Attached, { 0.0f, -0.2f, 2.0f });
            m_MainOverlay.SetOverlayInputMethod(vr::VROverlayInputMethod_None);
            m_MainOverlay.m_StateFlag = 3;
            if (m_Game->m_VRDebuglvl) m_Game->logMsg(LOGTYPE_DEBUG, "3D mode, Hud state");
        }

        m_Overlay->SetOverlayTexture(m_MainOverlay.m_Handle, &m_MenuTexture.m_VRTexture);
        m_MainOverlay.ShowOverlay();
    }
    else m_MainOverlay.HideOverlay();
    
    leftEye = vr::VRCompositor()->Submit(vr::Eye_Left, &m_LeftEye.m_VRTexture, &(m_TextureBounds)[0], vr::Submit_Default);
    rightEye = vr::VRCompositor()->Submit(vr::Eye_Right, &m_RightEye.m_VRTexture, &(m_TextureBounds)[1], vr::Submit_Default);

    if (m_Game->m_VRDebuglvl > 1)
    {
        if (leftEye != vr::VRCompositorError_None && leftEye != m_LastLeftEyeError)
        {
            m_Game->logMsg(LOGTYPE_ERROR, "2D mode left eye error: %s", CompositorErrorToString(leftEye));
            m_LastLeftEyeError = leftEye;
        }

        if (rightEye != vr::VRCompositorError_None && rightEye != m_LastRightEyeError)
        {
            m_Game->logMsg(LOGTYPE_ERROR, "2D mode right eye error: %s", CompositorErrorToString(rightEye));
            m_LastRightEyeError = rightEye;
        }
    }
}

//Converts the raw pose data to usable pose data
void VR::GetPoseData(const vr::TrackedDevicePose_t &poseRaw, TrackedDevicePoseData &poseOut)
{
    const vr::HmdMatrix34_t mat = poseRaw.mDeviceToAbsoluteTracking;

    poseOut.TrackedDevicePos.x = -mat.m[2][3];
    poseOut.TrackedDevicePos.y = -mat.m[0][3];
    poseOut.TrackedDevicePos.z = mat.m[1][3];

    poseOut.TrackedDeviceAng.x = asinf(mat.m[1][2]) * PRECALC_RAD_TO_DEG;
    poseOut.TrackedDeviceAng.y = atan2f(mat.m[0][2], mat.m[2][2]) * PRECALC_RAD_TO_DEG;
    poseOut.TrackedDeviceAng.z = atan2f(-mat.m[1][0], mat.m[1][1]) * PRECALC_RAD_TO_DEG;

    poseOut.TrackedDeviceVel.x = -poseRaw.vVelocity.v[2];
    poseOut.TrackedDeviceVel.y = -poseRaw.vVelocity.v[0];
    poseOut.TrackedDeviceVel.z = poseRaw.vVelocity.v[1];

    poseOut.TrackedDeviceAngVel.x = -poseRaw.vAngularVelocity.v[2] * PRECALC_RAD_TO_DEG;
    poseOut.TrackedDeviceAngVel.y = -poseRaw.vAngularVelocity.v[0] * PRECALC_RAD_TO_DEG;
    poseOut.TrackedDeviceAngVel.z = poseRaw.vAngularVelocity.v[1] * PRECALC_RAD_TO_DEG;
}

void VR::RepositionOverlay(vr::VROverlayHandle_t overlay, vr::TrackedDeviceIndex_t referenceDevice, OverlayRel con, Vector offset, OverlayRotation rot)
{
    vr::ETrackingUniverseOrigin trackingOrigin = vr::VRCompositor()->GetTrackingSpace();
    vr::HmdMatrix34_t device = m_Poses[referenceDevice].mDeviceToAbsoluteTracking;

    Vector deviceRight = { device.m[0][0], device.m[1][0], device.m[2][0] };
    Vector deviceUp = { device.m[0][1], device.m[1][1], device.m[2][1] };
    Vector deviceForward = { device.m[0][2], device.m[1][2], device.m[2][2] };
    Vector devicePos = { device.m[0][3], device.m[1][3], device.m[2][3] };

    VectorNormalize(deviceRight);
    VectorNormalize(deviceUp);
    VectorNormalize(deviceForward);

    Vector finalPos{};

    switch (con)
    {
        case OverlayRel_WorldSpace:
        {
            Vector trackingRight = { 1, 0, 0 };
            Vector trackingUp = { 0, 1, 0 };
            Vector trackingForward = { 0, 0, -1 };

            finalPos =
                trackingRight * offset.x +
                trackingUp * offset.y +
                trackingForward * offset.z;
            break;
        }
        case OverlayRel_DeviceSpace:
        {
            finalPos = devicePos + offset;
            break;
        }
        case OverlayRel_DeviceSpaceForward:
        {
            finalPos =
                devicePos +
                deviceRight * offset.x +
                deviceUp * offset.y -
                deviceForward * offset.z;
            break;
        }
        case OverlayRel_Attached:
        {
            finalPos = Vector(offset.x, offset.y, -offset.z);
            break;
        }
        default:
            return;
    }

    //Rotation flags
    bool inheritRotation = (con != OverlayRel_Attached);

    float yaw =
        (inheritRotation && (rot.flags & RotFlag_UseYaw))
        ? atan2f(deviceForward.x, deviceForward.z) + rot.yawOffset
        : rot.yawOffset;

    float pitch =
        (inheritRotation && (rot.flags & RotFlag_UsePitch))
        ? -atan2f(deviceForward.y, sqrtf(deviceForward.x * deviceForward.x + deviceForward.z * deviceForward.z)) + 
        rot.pitchOffset : rot.pitchOffset;

    float roll =
        (inheritRotation && (rot.flags & RotFlag_UseRoll))
        ? atan2f(deviceRight.y, deviceUp.y) + rot.rollOffset
        : rot.rollOffset;

    float cy = cosf(yaw);
    float sy = sinf(yaw);

    float cp = cosf(pitch);
    float sp = sinf(pitch);

    float cr = cosf(roll);
    float sr = sinf(roll);

    Vector forward;
    forward.x = sy * cp;
    forward.y = -sp;
    forward.z = cy * cp;

    Vector right;
    right.x = cy * cr + sy * sp * sr;
    right.y = cp * sr;
    right.z = -sy * cr + cy * sp * sr;

    Vector up;
    up.x = -cy * sr + sy * sp * cr;
    up.y = cp * cr;
    up.z = sy * sr + cy * sp * cr;

    VectorNormalize(right);
    VectorNormalize(up);
    VectorNormalize(forward);

    vr::HmdMatrix34_t transform{};

    Vector scaledRight = right * m_WScaleDownRatio;
    Vector scaledUp = up * m_HScaleDownRatio;

    transform.m[0][0] = scaledRight.x;
    transform.m[1][0] = scaledRight.y;
    transform.m[2][0] = scaledRight.z;

    transform.m[0][1] = scaledUp.x;
    transform.m[1][1] = scaledUp.y;
    transform.m[2][1] = scaledUp.z;

    transform.m[0][2] = forward.x;
    transform.m[1][2] = forward.y;
    transform.m[2][2] = forward.z;

    transform.m[0][3] = finalPos.x;
    transform.m[1][3] = finalPos.y;
    transform.m[2][3] = finalPos.z;

    if(!inheritRotation)
        m_Overlay->SetOverlayTransformTrackedDeviceRelative(overlay, referenceDevice, &transform);

    else
        m_Overlay->SetOverlayTransformAbsolute(overlay, trackingOrigin, &transform);
}

//Gets raw pose data
void VR::GetPoses()
{
    vr::TrackedDeviceIndex_t leftControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
    vr::TrackedDeviceIndex_t rightControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
    
    if (m_LeftHanded)
        std::swap(leftControllerIndex, rightControllerIndex);

    GetPoseData(m_Poses[vr::k_unTrackedDeviceIndex_Hmd], m_HmdPose);
    GetPoseData(m_Poses[leftControllerIndex], m_LeftControllerPose);
    GetPoseData(m_Poses[rightControllerIndex], m_RightControllerPose);
}

//Gets poses and inputs
void VR::UpdatePosesAndActions() 
{
    m_Input->UpdateActionState(&m_ActiveActionSet, sizeof(vr::VRActiveActionSet_t), 1);
    vr::VRCompositor()->WaitGetPoses(m_Poses, vr::k_unMaxTrackedDeviceCount, NULL, 0);
}

void VR::GetViewParameters() 
{
    vr::HmdMatrix34_t eyeToHeadLeft = m_System->GetEyeToHeadTransform(vr::Eye_Left);
    vr::HmdMatrix34_t eyeToHeadRight = m_System->GetEyeToHeadTransform(vr::Eye_Right);
    m_EyeToHeadTransformPosLeft.x = eyeToHeadLeft.m[0][3];
    m_EyeToHeadTransformPosLeft.y = eyeToHeadLeft.m[1][3];
    m_EyeToHeadTransformPosLeft.z = eyeToHeadLeft.m[2][3];

    m_EyeToHeadTransformPosRight.x = eyeToHeadRight.m[0][3];
    m_EyeToHeadTransformPosRight.y = eyeToHeadRight.m[1][3];
    m_EyeToHeadTransformPosRight.z = eyeToHeadRight.m[2][3];
}

bool VR::PressedDigitalAction(vr::VRActionHandle_t &actionHandle, bool checkIfActionChanged)
{
    vr::InputDigitalActionData_t digitalActionData;
    vr::EVRInputError result = m_Input->GetDigitalActionData(actionHandle, &digitalActionData, sizeof(digitalActionData), vr::k_ulInvalidInputValueHandle);
    
    if (result == vr::VRInputError_None)
    {
        if (checkIfActionChanged)
            return digitalActionData.bState && digitalActionData.bChanged;
        else
            return digitalActionData.bState;
    }

    return false;
}

bool VR::GetAnalogActionData(vr::VRActionHandle_t &actionHandle, vr::InputAnalogActionData_t &analogDataOut)
{
    vr::EVRInputError result = m_Input->GetAnalogActionData(actionHandle, &analogDataOut, sizeof(analogDataOut), vr::k_ulInvalidInputValueHandle);

    if (result == vr::VRInputError_None)
        return true;

    return false;
}

void VR::ProcessMenuInput()
{
    // Check if left or right hand controller is pointing at the overlay
    const bool isHoveringOverlay = CheckOverlayIntersectionForController(m_MainOverlay.m_Handle, vr::TrackedControllerRole_LeftHand) ||
                                   CheckOverlayIntersectionForController(m_MainOverlay.m_Handle, vr::TrackedControllerRole_RightHand);

    // Overlays can't process action inputs if the laser is active, so
    // only activate laser if a controller is pointing at the overlay
    if (isHoveringOverlay)
    {
        m_Overlay->SetOverlayFlag(m_MainOverlay.m_Handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);

        vr::VREvent_t vrEvent;
        while (m_Overlay->PollNextOverlayEvent(m_MainOverlay.m_Handle, &vrEvent, sizeof(vrEvent)))
        {
            INPUT input;
            switch (vrEvent.eventType)
            {
                case vr::VREvent_MouseMove:
                {
                    float laserX = (vrEvent.data.mouse.x / m_RenderWidth) * m_Game->m_WindowWidth;
                    float laserY = ((-vrEvent.data.mouse.y + m_RenderHeight) / m_RenderHeight) * m_Game->m_WindowHeight;

                    m_Game->m_VguiInput->SetCursorPos(laserX, laserY);
                    break;
                }
                case vr::VREvent_MouseButtonDown:
                {
                    // Don't allow holding down the mouse down in the pause menu. The resume button can be clicked before
                    // the MouseButtonUp event is polled, which causes issues with the overlay.
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                    break;
                }
                case vr::VREvent_MouseButtonUp:
                {
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(1, &input, sizeof(INPUT));
                    break;
                }
                case vr::VREvent_ScrollDiscrete:
                {
                    m_Game->m_VguiInput->InternalMouseWheeled((int)vrEvent.data.scroll.ydelta);
                    break;
                }
            }
        }
    }
    else
    {
        m_Overlay->SetOverlayFlag(m_MainOverlay.m_Handle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);

        for (auto& binding : m_Bindings)
        {
            if (binding.m_BindingType == VRBindingType_Menu)
            {
                bool Pressed = PressedDigitalAction(binding.m_Handle, true);

                if (binding.m_HoldPress)
                {
                    if (Pressed && !binding.m_LastButtonState)
                    {
                        binding.m_HoldState = !binding.m_HoldState;

                        if (binding.m_HoldState)
                        {
                            if (binding.m_PressCommand) m_Game->ClientCmd_Unrestricted(binding.m_PressCommand);
                            binding.m_Func(binding.m_Handle);
                        }
                        else if (binding.m_ReleaseCommand) m_Game->ClientCmd_Unrestricted(binding.m_ReleaseCommand);
                    }

                    binding.m_LastButtonState = Pressed;
                }
                else
                {
                    if (Pressed)
                    {
                        if (binding.m_PressCommand) m_Game->ClientCmd_Unrestricted(binding.m_PressCommand);
                        binding.m_Func(binding.m_Handle);
                    }
                    else if (binding.m_ReleaseCommand) m_Game->ClientCmd_Unrestricted(binding.m_ReleaseCommand);
                }
            }
        }
    }
}

void VR::ProcessInput()
{
    for (auto& binding : m_Bindings)
    {
        if (binding.m_BindingType == VRBindingType_Analog)
        {
            binding.m_Func(binding.m_Handle);
            continue;
        }

        else if (binding.m_BindingType == VRBindingType_Input)
        {
            bool Pressed = PressedDigitalAction(binding.m_Handle, true);

            if (binding.m_HoldPress)
            {
                if (Pressed && !binding.m_LastButtonState)
                {
                    binding.m_HoldState = !binding.m_HoldState;

                    if (binding.m_HoldState)
                    {
                        if (binding.m_PressCommand) m_Game->ClientCmd_Unrestricted(binding.m_PressCommand);
                        binding.m_Func(binding.m_Handle);
                    }
                    else if (binding.m_ReleaseCommand)
                        m_Game->ClientCmd_Unrestricted(binding.m_ReleaseCommand);
                }

                binding.m_LastButtonState = Pressed;
            }
            else
            {
                if (Pressed)
                {
                    if (binding.m_PressCommand) m_Game->ClientCmd_Unrestricted(binding.m_PressCommand);
                    binding.m_Func(binding.m_Handle);
                }
                else if (binding.m_ReleaseCommand)
                    m_Game->ClientCmd_Unrestricted(binding.m_ReleaseCommand);
            }
        }
    }

    // Re-align camera upright after portalling
    if (m_RotationOffset.x != 0.f || m_RotationOffset.z != 0.f)
    {
        // Last valid yaw angle, in case we need to revert to it.
        const float lastYaw = m_RotationOffset.y;

        // Get normalized forward direction vector from current rotation offset:
        Vector fwdSrc;
        QAngle::AngleVectors(m_RotationOffset, &fwdSrc, nullptr, nullptr);
        VectorNormalize(fwdSrc);

        // Get normalized forward direction vector from target rotation offset:
        const QAngle targetRotation(0, lastYaw, 0);
        Vector fwdTarget;
        QAngle::AngleVectors(targetRotation, &fwdTarget, nullptr, nullptr);
        VectorNormalize(fwdTarget);

        // Slerp taken from `glm/rotate_vector.inl`:
        const auto slerp = [](const Vector& x, const Vector& y, float a) -> Vector {
            // get cosine of angle between vectors (-1 -> 1)
            auto CosAlpha = DotProduct(x, y);
            // get angle (0 -> pi)
            auto Alpha = std::acos(CosAlpha);
            // get sine of angle between vectors (0 -> 1)
            auto SinAlpha = std::sin(Alpha);
            // this breaks down when SinAlpha = 0, i.e. Alpha = 0 or pi

            if (SinAlpha == 0.f) {
                return y;
            }

            auto t1 = std::sin((1.f - a) * Alpha) / SinAlpha;
            auto t2 = std::sin(a * Alpha) / SinAlpha;

            // interpolate src vectors
            return x * t1 + y * t2;
            };

        // Calculate slerp between source and target direction vectors:
        Vector slerped = slerp(fwdSrc, fwdTarget, m_CameraUprightRecoverySpeed);
        VectorNormalize(slerped);

        // Turn the final direction vector into Euler angles and apply it:
        QAngle finalAngles;
        QAngle::VectorAngles(slerped, finalAngles);
        m_RotationOffset = finalAngles;

        // Just in case any calculation went awry, revert to an upright vector:
        if (std::isnan(m_RotationOffset.x) || std::isnan(m_RotationOffset.y) || std::isnan(m_RotationOffset.z))
        {
            m_RotationOffset = QAngle(0.f, lastYaw, 0.f);
        }
    }
}

VMatrix VR::VMatrixFromHmdMatrix(const vr::HmdMatrix34_t &hmdMat)
{
    // VMatrix has a different implicit coordinate system than HmdMatrix34_t, but this function does not convert between them
    VMatrix vMat(
        hmdMat.m[0][0], hmdMat.m[1][0], hmdMat.m[2][0], 0.0f,
        hmdMat.m[0][1], hmdMat.m[1][1], hmdMat.m[2][1], 0.0f,
        hmdMat.m[0][2], hmdMat.m[1][2], hmdMat.m[2][2], 0.0f,
        hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3], 1.0f
    );

    return vMat;
}

vr::HmdMatrix34_t VR::VMatrixToHmdMatrix(const VMatrix &vMat)
{
    vr::HmdMatrix34_t hmdMat = {0};

    hmdMat.m[0][0] = vMat.m[0][0];
    hmdMat.m[1][0] = vMat.m[0][1];
    hmdMat.m[2][0] = vMat.m[0][2];

    hmdMat.m[0][1] = vMat.m[1][0];
    hmdMat.m[1][1] = vMat.m[1][1];
    hmdMat.m[2][1] = vMat.m[1][2];

    hmdMat.m[0][2] = vMat.m[2][0];
    hmdMat.m[1][2] = vMat.m[2][1];
    hmdMat.m[2][2] = vMat.m[2][2];

    hmdMat.m[0][3] = vMat.m[3][0];
    hmdMat.m[1][3] = vMat.m[3][1];
    hmdMat.m[2][3] = vMat.m[3][2];

    return hmdMat;
}

vr::HmdMatrix34_t VR::GetControllerTipMatrix(vr::ETrackedControllerRole controllerRole)
{
    vr::VRInputValueHandle_t inputValue = vr::k_ulInvalidInputValueHandle;

    if (controllerRole == vr::TrackedControllerRole_RightHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/right", &inputValue);
    }
    else if (controllerRole == vr::TrackedControllerRole_LeftHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/left", &inputValue);
    }

    if (inputValue != vr::k_ulInvalidInputValueHandle)
    {
#ifdef GetControllerTipMatrix_HeapBuffer
        std::vector<char> buffer(vr::k_unMaxPropertyStringSize);
        char* bufferPtr = buffer.data();
#else
        char buffer[vr::k_unMaxPropertyStringSize];
        char* bufferPtr = buffer;
#endif

        m_System->GetStringTrackedDeviceProperty(vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(controllerRole), vr::Prop_RenderModelName_String, 
                                                 bufferPtr, vr::k_unMaxPropertyStringSize);

        vr::RenderModel_ControllerMode_State_t controllerState = {0};
        vr::RenderModel_ComponentState_t componentState = {0};

        if (vr::VRRenderModels()->GetComponentStateForDevicePath(bufferPtr, vr::k_pch_Controller_Component_Tip, inputValue, &controllerState, &componentState))
        {
            return componentState.mTrackingToComponentLocal;
        }
    }

    // Not a hand controller role or tip lookup failed, return identity
    const vr::HmdMatrix34_t identity = 
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    return identity;
}

bool VR::CheckOverlayIntersectionForController(vr::VROverlayHandle_t overlayHandle, vr::ETrackedControllerRole controllerRole)
{
    vr::TrackedDeviceIndex_t deviceIndex = m_System->GetTrackedDeviceIndexForControllerRole(controllerRole);

    if (deviceIndex == vr::k_unTrackedDeviceIndexInvalid)
        return false;

    vr::TrackedDevicePose_t &controllerPose = m_Poses[deviceIndex];

    if (!controllerPose.bPoseIsValid)
        return false;

    VMatrix controllerVMatrix = VMatrixFromHmdMatrix(controllerPose.mDeviceToAbsoluteTracking);
    VMatrix tipVMatrix        = VMatrixFromHmdMatrix(GetControllerTipMatrix(controllerRole));
    tipVMatrix.MatrixMul(controllerVMatrix, controllerVMatrix);

    vr::VROverlayIntersectionParams_t  params  = {0};
    vr::VROverlayIntersectionResults_t results = {0};

    params.eOrigin    = vr::VRCompositor()->GetTrackingSpace();
    params.vSource    = { controllerVMatrix.m[3][0],  controllerVMatrix.m[3][1],  controllerVMatrix.m[3][2]};
    params.vDirection = {-controllerVMatrix.m[2][0], -controllerVMatrix.m[2][1], -controllerVMatrix.m[2][2]};

    return m_Overlay->ComputeOverlayIntersection(overlayHandle, &params, &results);
}

QAngle VR::GetRightControllerAbsAngle()
{
    return m_RightControllerAngAbs;
}

QAngle& VR::GetRightControllerAbsAngleConst()
{
    return m_RightControllerAngAbs;
}

Vector VR::GetRightControllerAbsPos(Vector eyePosition)
{
    Vector offset = eyePosition;

    if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
        /*int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
        C_BasePlayer* localPlayer = (C_BasePlayer*)m_Game->GetClientEntity(playerIndex);
        if (!localPlayer)
            return {0, 0, 0};

        offset = localPlayer->EyePosition();*/

        offset = m_SetupOrigin;
    }

    Vector position = offset + m_RightControllerPosRel;

    if (m_6DOF)
        position += m_HmdPosRelative;

    return position;
}

Vector VR::GetRecommendedViewmodelAbsPos(Vector eyePosition)
{
    Vector viewmodelPos = GetRightControllerAbsPos(eyePosition);
    viewmodelPos -= m_ViewmodelForward * m_ViewmodelPosOffset.x;
    viewmodelPos -= m_ViewmodelRight * m_ViewmodelPosOffset.y;
    viewmodelPos -= m_ViewmodelUp * m_ViewmodelPosOffset.z;

    return viewmodelPos;
}

QAngle VR::GetRecommendedViewmodelAbsAngle()
{
    QAngle result{};

    QAngle::VectorAngles(m_ViewmodelForward, m_ViewmodelUp, result);

    return result;
}

void VR::UpdateHMDAngles() {
    QAngle hmdAngLocal = m_HmdPose.TrackedDeviceAng;

    //hmdAngLocal += m_RotationOffset;
    hmdAngLocal.x += m_RotationOffset.x;
    hmdAngLocal.y += m_RotationOffset.y;
    hmdAngLocal.z += m_RotationOffset.z;

    //hmdAngLocal.Normalize();

    QAngle::AngleVectors(hmdAngLocal, &m_HmdForward, &m_HmdRight, &m_HmdUp);

    //hmdAngLocal.x = (hmdAngLocal.x > 180 ? 180)
    hmdAngLocal.Normalize();

    m_HmdAngAbs = hmdAngLocal;
}

void VR::ResetPosition()
{
    m_Center = m_HmdPose.TrackedDevicePos;
}

void VR::UpdateTracking()
{
    GetPoses();

    C_BasePlayer* localPlayer = m_Game->GetPlayer();
    if (!localPlayer)
        return;

    // HMD tracking
    Vector hmdPosLocal = m_HmdPose.TrackedDevicePos;
    Vector hmdPosCentered = hmdPosLocal - m_Center;

    m_HmdPosRelativeRaw = hmdPosCentered;

    //std::cout << "HMD - X: " << hmdWorldPos.x << ", Y: " << hmdWorldPos.y << ", Z: " << hmdWorldPos.z << "\n";

    Vector hmdPosCorrected = hmdPosCentered;
    VectorPivotXY(hmdPosCorrected, { 0, 0, 0 }, m_RotationOffset.y);

    UpdateHMDAngles();

    m_HmdPosRelative = hmdPosCorrected * m_VRScale;

    // Roomscale setup
    /*Vector cameraMovingDirection = m_Center - m_SetupOriginPrev;
    Vector cameraToPlayer = m_HmdPosAbsPrev - m_SetupOriginPrev;
    cameraMovingDirection.z = 0;
    cameraToPlayer.z = 0;
    float cameraFollowing = DotProduct(cameraMovingDirection, cameraToPlayer);
    float cameraDistance = VectorLength(cameraToPlayer);

    if (localPlayer->m_hGroundEntity != -1 && localPlayer->m_vecVelocity.IsZero())
        m_RoomscaleActive = true;

    // TODO: Get roomscale to work while using thumbstick
    if ((cameraFollowing < 0 && cameraDistance > 1) || (m_PushingThumbstick))
        m_RoomscaleActive = false;*/


    //Laser pointer thingy
    if (m_AimMode == 2) {
        m_AimPos = Trace((uint32_t*)localPlayer);
        C_Portal_Player* portalPlayer = m_Game->GetPortalPlayer(localPlayer);
        CWeaponPortalBase* activeWeapon = portalPlayer->GetActivePortalWeapon();

        if (activeWeapon) {
            if (portalPlayer->m_PointLaser) {
                portalPlayer->m_PointLaser->SetControlPoint(1, m_AimPos);
                portalPlayer->m_PointLaser->SetControlPoint(2, m_Game->m_singlePlayerPortalColors[activeWeapon->m_iLastFiredPortal] * 0.5f);
            }
            else {
                if (!m_ParticleCreated)
                {
                    m_Game->m_Hooks->PrecacheParticleSystem("robot_point_beam");
                    m_ParticleCreated = true;
                }

                m_Game->logMsg(LOGTYPE_DEBUG, "Creating Point Laser Beam Sight Thingy");
                m_Game->m_Hooks->CreatePingPointer(localPlayer, m_AimPos);
            }
        }
        else if (portalPlayer->m_PointLaser) {
            m_Game->logMsg(LOGTYPE_DEBUG, "Destroying Point Laser Beam Sight Thingy");
            portalPlayer->m_PointLaser->StopEmission(false, true);
            portalPlayer->m_PointLaser = NULL;
        }
    }

    // Check if camera is clipping inside wall
    /*CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    Vector extendedHmdPos = m_HmdPosAbs - m_SetupOrigin;
    VectorNormalize(extendedHmdPos);
    extendedHmdPos = m_HmdPosAbs + (extendedHmdPos * 10);
    ray.Init(m_SetupOrigin, extendedHmdPos);

    m_Game->m_EngineTrace->TraceRay(ray, STANDARD_TRACE_MASK, &tracefilter, &trace);
    if (trace.fraction < 1 && trace.fraction > 0)
    {
        Vector distanceInsideWall = trace.endpos - extendedHmdPos;
        m_CameraAnchor += distanceInsideWall;
        m_HmdPosAbs = m_CameraAnchor - Vector(0, 0, 64) + m_HmdPosLocalInWorld;
    }

    // Reset camera if it somehow gets too far
    m_SetupOriginToHMD = m_HmdPosAbs - m_SetupOrigin;
    if (VectorLength(m_SetupOriginToHMD) > 150)
        ResetPosition();

    m_HmdPosAbsPrev = m_HmdPosAbs;
    m_SetupOriginPrev = m_SetupOrigin;*/

    GetViewParameters();
    m_Ipd = m_EyeToHeadTransformPosRight.x * 2;
    m_EyeZ = m_EyeToHeadTransformPosRight.z;

    // Hand tracking
    Vector leftControllerPosLocal = m_LeftControllerPose.TrackedDevicePos;
    QAngle leftControllerAngLocal = m_LeftControllerPose.TrackedDeviceAng;

    Vector rightControllerPosLocal = m_RightControllerPose.TrackedDevicePos;
    QAngle rightControllerAngLocal = m_RightControllerPose.TrackedDeviceAng;

    //std::cout << "Right Controller - X: " << rightControllerPosLocal.x << "Y: " << rightControllerPosLocal.y << "Z: " << rightControllerPosLocal.z << "\n";

    Vector hmdToController = rightControllerPosLocal - hmdPosLocal;
    //Vector rightControllerPosCorrected = hmdPosCorrected + hmdToController;

    // When using stick turning, pivot the controllers around the HMD
    VectorPivotXY(hmdToController, { 0, 0, 0 }, m_RotationOffset.y);

    m_RightControllerPosRel = hmdToController * m_VRScale;

    //rightControllerAngLocal += m_RotationOffset;
    rightControllerAngLocal.x += m_RotationOffset.x;
    rightControllerAngLocal.y += m_RotationOffset.y;
    rightControllerAngLocal.z += m_RotationOffset.z;

    // Wrap angle from -180 to 180
    //rightControllerAngLocal.Normalize();

    QAngle::AngleVectors(leftControllerAngLocal, &m_LeftControllerForward, &m_LeftControllerRight, &m_LeftControllerUp);
    QAngle::AngleVectors(rightControllerAngLocal, &m_RightControllerForward, &m_RightControllerRight, &m_RightControllerUp);

    const float offset = -30;

    // Adjust controller angle downward
    m_LeftControllerForward = VectorRotate(m_LeftControllerForward, m_LeftControllerRight, offset);
    m_LeftControllerUp = VectorRotate(m_LeftControllerUp, m_LeftControllerRight, offset);

    m_RightControllerForward = VectorRotate(m_RightControllerForward, m_RightControllerRight, offset);
    m_RightControllerUp = VectorRotate(m_RightControllerUp, m_RightControllerRight, offset);

    // controller angles
    QAngle::VectorAngles(m_LeftControllerForward, m_LeftControllerUp, m_LeftControllerAngAbs);
    QAngle::VectorAngles(m_RightControllerForward, m_RightControllerUp, m_RightControllerAngAbs);
    m_RightControllerAngAbs.Normalize();

    PositionAngle viewmodelOffset = PositionAngle{ {4.5, -1, 1.5}, {0,0,0} };

    // Apply both hardcoded and custom (from config) viewmodel offsets here:
    m_ViewmodelPosOffset = viewmodelOffset.position + m_ViewmodelPosCustomOffset;
    m_ViewmodelAngOffset = viewmodelOffset.angle + m_ViewmodelAngCustomOffset;

    m_ViewmodelForward = m_RightControllerForward;
    m_ViewmodelUp = m_RightControllerUp;
    m_ViewmodelRight = m_RightControllerRight;

    // Viewmodel yaw offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelUp, m_ViewmodelAngOffset.y);
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelUp, m_ViewmodelAngOffset.y);

    // Viewmodel pitch offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelRight, m_ViewmodelAngOffset.x);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelRight, m_ViewmodelAngOffset.x);

    // Viewmodel roll offset
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelForward, m_ViewmodelAngOffset.z);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelForward, m_ViewmodelAngOffset.z);
}

Vector VR::GetViewAngle()
{
    return Vector( m_HmdAngAbs.x, m_HmdAngAbs.y, m_HmdAngAbs.z );
}

Vector VR::GetViewOrigin(Vector setupOrigin)
{
    Vector center = setupOrigin;

    if (m_6DOF)
        center += m_HmdPosRelative;

    return center + (m_HmdForward * -(m_EyeZ * m_VRScale));
}

Vector VR::GetViewOriginLeft(Vector setupOrigin)
{
    Vector viewOriginLeft = GetViewOrigin(setupOrigin);
    viewOriginLeft -= m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginLeft;
}

Vector VR::GetViewOriginRight(Vector setupOrigin)
{
    Vector viewOriginRight = GetViewOrigin(setupOrigin);
    viewOriginRight += m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginRight;
}

Vector VR::Trace(uint32_t* localPlayer) {
    Vector vecStart = GetRightControllerAbsPos();
    Vector vecEnd = vecStart + m_RightControllerForward * MAX_TRACE_LENGTH;

    CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(vecStart, vecEnd);

    m_Game->m_EngineTrace->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trace);

    return trace.endpos;
}

void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{
    float sr, sp, sy, cr, cp, cy;

    SinCos(angles[YAW] * PRECALC_DEG_TO_RAD, &sy, &cy);
    SinCos(angles[PITCH] * PRECALC_DEG_TO_RAD, &sp, &cp);
    SinCos(angles[ROLL] * PRECALC_DEG_TO_RAD, &sr, &cr);

    // matrix = (YAW * PITCH) * ROLL
    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;

    // NOTE: Do not optimize this to reduce multiplies! optimizer bug will screw this up.
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = (cr * sp * cy + -sr * -sy);
    matrix[1][2] = (cr * sp * sy + -sr * cy);
    matrix[2][2] = cr * cp;

    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
    memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}


/*
================
R_ConcatTransforms
================
*/

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
    if (&in1 == &out)
    {
        matrix3x4_t in1b;
        MatrixCopy(in1, in1b);
        ConcatTransforms(in1b, in2, out);
        return;
    }
    if (&in2 == &out)
    {
        matrix3x4_t in2b;
        MatrixCopy(in2, in2b);
        ConcatTransforms(in1, in2b, out);
        return;
    }
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
        in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
        in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
        in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
        in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
        in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
        in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
        in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
        in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
        in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
        in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
        in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
        in1[2][2] * in2[2][3] + in1[2][3];
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
    float forward[3];
    float left[3];
    float up[3];

    //
    // Extract the basis vectors from the matrix. Since we only need the Z
    // component of the up vector, we don't get X and Y.
    //
    forward[0] = matrix[0][0];
    forward[1] = matrix[1][0];
    forward[2] = matrix[2][0];
    left[0] = matrix[0][1];
    left[1] = matrix[1][1];
    left[2] = matrix[2][1];
    up[2] = matrix[2][2];

    float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

    // enough here to get angles?
    if (xyDist > 0.001f)
    {
        // (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
        angles[1] = atan2f(forward[1], forward[0]) * PRECALC_RAD_TO_DEG;

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = atan2f(-forward[2], xyDist) * PRECALC_RAD_TO_DEG;

        // (roll)	z = ATAN( left.z, up.z );
        angles[2] = atan2f(left[2], up[2]) * PRECALC_RAD_TO_DEG;
    }
    else	// forward is mostly Z, gimbal lock-
    {
        // (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
        angles[1] = atan2f(-left[0], left[1]) * PRECALC_RAD_TO_DEG;

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = atan2f(-forward[2], xyDist) * PRECALC_RAD_TO_DEG;

        // Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
        angles[2] = 0;
    }
}

inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
{
    MatrixAngles(matrix, &angles.x);
}



// transform a set of angles in the input space of parentMatrix to the output space
QAngle TransformAnglesToWorldSpace(const QAngle& angles, const matrix3x4_t& parentMatrix)
{
    matrix3x4_t angToParent, angToWorld;
    AngleMatrix(angles, angToParent);
    ConcatTransforms(parentMatrix, angToParent, angToWorld);
    QAngle out;
    MatrixAngles(angToWorld, out);
    return out;
}


Vector VR::TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle) {
    CGameTrace trTestObstructionsNearPortals;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(cameraPos, eyePos);
    m_Game->m_EngineTrace->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trTestObstructionsNearPortals);

    float flWallHitFraction = trTestObstructionsNearPortals.fraction + 0.01f;
    CPortal_Base2D* pPortal = (CPortal_Base2D*)m_Game->m_Hooks->UTIL_Portal_FirstAlongRay(ray, flWallHitFraction);

    if (trTestObstructionsNearPortals.DidHit() && pPortal) {
        float flRayHitFraction = m_Game->m_Hooks->UTIL_IntersectRayWithPortal(ray, pPortal);
        //Vector vNewEye;
        Vector vHitPoint = ray.m_Start + ray.m_Delta * flRayHitFraction;
        //vNewEye = m_Game->m_Hooks->UTIL_Portal_PointTransform(pPortal->MatrixThisToLinked(), vHitPoint, vNewEye);

        //VMatrix matrix = *(VMatrix*)((uintptr_t)pPortal + 0x4C4);
        VMatrix matrix = pPortal->MatrixThisToLinked();

   
        /*QAngle newAngle;
        m_Game->m_Hooks->UTIL_Portal_AngleTransform(matrix, eyeAngle, newAngle);*/
        eyeAngle = TransformAnglesToWorldSpace(eyeAngle, matrix.As3x4());

        return matrix * vHitPoint;

        //return pPortal->MatrixThisToLinked() * vHitPoint;
    }

    return cameraPos;
}

// [CONFIG PARSING UTILITY FUNCTION]
// Generates an error message by stringifying and concatenating 'args...'.
template <typename... Ts>
static void concatErrorMsg(Game& game, const Ts&... args)
{
    std::ostringstream oss;
    (oss << ... << args);
    game.errorMsg(oss.str().c_str());
}

// [CONFIG PARSING UTILITY FUNCTION]
// Attempts to parse an entry with key 'key' from the provided 'userConfig'. If the key is
// missing or if the parsing fails, 'defaultValue' is returned and an error message is
// generated.
template <typename T>
static T parseConfigEntry(const std::unordered_map<std::string, std::string>& userConfig, Game& game, const char* key, const T& defaultValue)
try
{
    const auto itr = userConfig.find(key);

    if (itr == userConfig.end())
    {
        concatErrorMsg(game, "Config entry with key '", key,
            "' missing -- reverting to default value of '", defaultValue, "'");

        return defaultValue;
    }

    const std::string& configValue = itr->second;

    if constexpr (std::is_same_v<T, bool>)
    {
        std::string val = configValue;

        val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);

        return val == "true";
    }
    else if constexpr(std::is_floating_point_v<T>)
    {
        return std::stof(configValue);
    }
    else if constexpr(std::is_integral_v<T>)
    {
        return std::stol(configValue);
    }
    else
    {
        // Just a way of generating a compilation failure in case this branch is taken.
        struct invalid_type;
        return invalid_type{};
    }
}
catch (const std::logic_error& e)
{
    concatErrorMsg(game, "Error parsing config entry with key '", key,
        "' -- reverting to default value of '", defaultValue, "' -- error: (", e.what(), ")");

    throw;
}

void VR::ParseConfigFile()
{
    std::ifstream configStream("VR\\config.txt");
    std::unordered_map<std::string, std::string> userConfig;

    std::string line;
    while (std::getline(configStream, line))
    {
        std::istringstream sLine(line);
        std::string key;
        if (std::getline(sLine, key, '='))
        {
            std::string value;
            if (std::getline(sLine, value, '#'))
                userConfig[key] = value;
            else if (std::getline(sLine, value))
                userConfig[key] = value;
        }
    }

    if (userConfig.empty())
        return;

    // Parse a single entry with key 'key' from the config into 'target'.
    // If the entry does not exist, or if the parsing fails, sets 'target' to
    // 'defaultValue'.
    const auto parseOrDefault = [&](const char* key, auto& target, const auto& defaultValue) 
    { 
        target = parseConfigEntry(userConfig, *m_Game, key, defaultValue);
        m_Game->logMsg(LOGTYPE_DEBUG, "Setting %s to %s", key, std::to_string(target).c_str());
    };

    // Parses a vector or angle from the config into 'target'. The XYZ coordinates
    // are read from three separate config entries with key 'keyPrefix' + 'X'/'Y'/'Z'.
    // If any entry does not exist, or if the parsing fails, sets the corresponding
    // coordinate in 'target' to zero.
    const auto parseVectorOrDefaultZero = [&](const char* key, auto& target)
    {
        auto it = userConfig.find(key);

        target.x = target.y = target.z = 0.0f;

        if (it != userConfig.end())
        {
            std::string value = it->second;
            value.erase(std::remove(value.begin(), value.end(), '{'), value.end());
            value.erase(std::remove(value.begin(), value.end(), '}'), value.end());

            std::replace(value.begin(), value.end(), ',', ' ');

            std::istringstream stream(value);

            if (!(stream >> target.x >> target.y >> target.z))
                target.x = target.y = target.z = 0.0f;
        }

        m_Game->logMsg(LOGTYPE_DEBUG, "Setting %s to { %.3f, %.3f, %.3f }", key, target.x, target.y, target.z);
    };

    parseOrDefault("SnapTurning", m_SnapTurning, false);
    parseOrDefault("SnapTurnAngle", m_SnapTurnAngle, 45.0f);
    parseOrDefault("TurnSpeed", m_TurnSpeed, 0.15f);
    parseOrDefault("LeftHanded", m_LeftHanded, false);
    parseOrDefault("VRScale", m_VRScale, 43.2f);
    parseOrDefault("IPDScale", m_IpdScale, 1.0f);
    parseOrDefault("6DOF", m_6DOF, true);
    parseOrDefault("AimMode", m_AimMode, 2);
    parseOrDefault("AntiAliasing", m_AntiAliasing, 0);
    parseOrDefault("RenderWindow", m_RenderWindow, false);
    if (m_Game->m_GameType == GAMETYPE_PORTAL2) parseOrDefault("Enable3DBackground", m_3DMenu, false);
    parseOrDefault("ExperimentalOptimizations", m_ExperimentalOptimizations, 0);
    parseOrDefault("PortallingDetectionDistanceThreshold", m_PortallingDetectionDistanceThreshold, 35);
    parseOrDefault("CameraUprightRecoverySpeed", m_CameraUprightRecoverySpeed, 0.2f);
    parseOrDefault("SmoothRotation", m_SmoothRotation, false);
    parseVectorOrDefaultZero("ViewmodelPosCustomOffset", m_ViewmodelPosCustomOffset);
    parseVectorOrDefaultZero("ViewmodelAngCustomOffset", m_ViewmodelAngCustomOffset);
}

void VR::WaitForConfigUpdate()
{
    char configDir[MAX_STR_LEN];
    sprintf_s(configDir, MAX_STR_LEN, "%s\\VR\\", m_Game->m_GameDir);
    HANDLE fileChangeHandle = FindFirstChangeNotificationA(configDir, false, FILE_NOTIFY_CHANGE_LAST_WRITE);

    std::filesystem::file_time_type configLastModified;
    while (1)
    {
        try 
        {
            // Windows only notifies of change within a directory, so extra check here for just config.txt
            auto configModifiedTime = std::filesystem::last_write_time("VR\\config.txt");
            if (configModifiedTime != configLastModified)
            {
                configLastModified = configModifiedTime;
                ParseConfigFile();
                
                m_Game->logMsg(LOGTYPE_DEBUG, "Successfully reloaded config.txt");
            }
        }
        catch (const std::invalid_argument &e)
        {
            concatErrorMsg(
                *m_Game, "Failed to parse 'config.txt' (", e.what(), ")");
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            concatErrorMsg(
                *m_Game, "'config.txt' not found. (", e.what(), ")");
            
            return;
        }
        
        FindNextChangeNotification(fileChangeHandle);
        WaitForSingleObject(fileChangeHandle, INFINITE);
        Sleep(100); // Sometimes the thread tries to read config.txt before it's finished writing
    }
}

std::string VR::GetMapFromSave(const char* fileName)
{
    std::ifstream file(fileName, std::ios::binary);
    if (!file.is_open())
        return "Save Parse Error: can't open file";

    // Jump directly to offset 4230
    constexpr std::streamoff mapOffset = 4230;
    file.seekg(mapOffset, std::ios::beg);
    if (!file)
        return "Save Parse Error: can't jump to offset";

    // Read printable characters until null, newline, or whitespace
    std::string mapName;
    char c;
    while (file.get(c))
    {
        if (c <= 32 || c >= 127)
            break;
        mapName += c;
    }
    
    return mapName;
}

std::string VR::GetNewestPortal2SavePath(const std::string& baseDir)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path saveRoot = fs::path(baseDir) / "portal2" / "SAVE" / std::to_string(m_SteamID);
    if (!fs::exists(saveRoot, ec) || !fs::is_directory(saveRoot, ec))
    {
        g_Game->logMsg(LOGTYPE_WARNING, "Failed to find save directory: %s", ec.message());
        return "";
    }

    fs::path newestFile;
    fs::file_time_type newestTime;
    for (const auto& entry : fs::directory_iterator(saveRoot, ec))
    {
        if (ec)
        {
            g_Game->logMsg(LOGTYPE_WARNING, "Error iterating save folder: %s", ec.message());
            break;
        }

        if (!entry.is_regular_file(ec))
        {
            if (ec)
                g_Game->logMsg(LOGTYPE_WARNING, "Error checking if file is regular: %s", ec.message());
            continue;
        }

        if (entry.path().extension() != ".sav")
            continue;

        auto writeTime = fs::last_write_time(entry, ec);
        if (ec)
        {
            g_Game->logMsg(LOGTYPE_WARNING, "Error reading file time: %s", ec.message());
            continue;
        }

        if (newestFile.empty() || writeTime > newestTime)
        {
            newestTime = writeTime;
            newestFile = entry.path();
        }
    }
    if (newestFile.empty())
        g_Game->logMsg(LOGTYPE_WARNING, "No save files found");
        
    return newestFile.empty() ? "" : newestFile.string();
}

//Checks and gets if the 3D menu background is loaded and loads it if needed
int VR::Load3DMenu() 
{
    std::string SaveFile = GetNewestPortal2SavePath(m_Game->m_GameDir);
    if (SaveFile.empty())
        return -1;

    std::string SaveMap = GetMapFromSave(SaveFile.c_str());
    if (SaveMap.empty())
        return -1;

    std::string backgroundMap;
    auto it = m_BackgroundMapping.find(SaveMap);
    if (it == m_BackgroundMapping.end())
    {
        m_Game->logMsg(LOGTYPE_WARNING, "%s is not mapped to any backgrounds.", SaveMap.c_str());
        return -1;
    }
        
    backgroundMap = it->second;
    m_Game->logMsg(LOGTYPE_DEBUG, "Loading background: %s", backgroundMap.c_str());
    m_Game->m_EngineClient->ClientCmd_Unrestricted(("map_background " + backgroundMap).c_str());
    return 0;
}

bool VR::ShouldCapture(CaptureConditions con)
{
    switch (con)
    {
        case Capture_Any: return true;
        case Capture_2D: return !g_Game->m_EngineClient->IsInGame();
        case Capture_MenuUI: return m_IsLevelBackground || m_Game->m_EngineClient->IsPaused();
        case Capture_HudUI: return (m_IsCredits || !m_Game->m_EngineClient->IsPaused()) && m_Game->m_EngineClient->IsInGame() && !m_IsLevelBackground;
    }
     
    return false;
}

void VR::BuildCaptureMap()
{
    RegisterPanelCaptureRoot(m_Game->m_EnginePanel->GetPanel(PANEL_GAMEUIDLL), m_MenuTexture.m_MSAAITex,
        [this]() { return this->ShouldCapture(Capture_MenuUI); });

    RegisterPanelCaptureRoot(m_Game->m_EnginePanel->GetPanel(PANEL_CLIENTDLL), m_MenuTexture.m_MSAAITex,
        [this]() { return this->ShouldCapture(Capture_HudUI); });

    VPANEL panel = FindParentOf(m_Game->m_EnginePanel->GetPanel(PANEL_CLIENTDLL), "HudWeapon");
    RegisterPanelCaptureRoot(panel, m_MenuTexture.m_MSAAITex, [this]() { return this->ShouldCapture(Capture_HudUI); },
    { 
        std::make_pair("HudCrosshair", m_HudTexture.m_ITex),
        std::make_pair("HUDQuickInfo", m_HudTexture.m_ITex),
        std::make_pair("HudWeapon", m_HudTexture.m_ITex),
        std::make_pair("HUDAutoAim", m_HudTexture.m_ITex)
    });
   
    m_BuiltCaptureMap = true;
}

void VR::CreateRT(SharedTextureHolder* target, const char* name, int w, int h, RenderTargetSizeMode_t sizeMode, ImageFormat format, MaterialRenderTargetDepth_t depth, UINT textureFlags)
{
    if (m_Game->m_VRDebuglvl) m_Game->logMsg(LOGTYPE_DEBUG, "CreateRT: %s, W: %d, H: %d", name, w, h);
    target->Release();

    PushTexture(target, false);
    target->m_ITex = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx(name, w, h, sizeMode, format, depth, textureFlags);

    bool hasOverride = target->m_OverrideMSAASurface.has_value();
    if (target->m_UseMSAA || hasOverride)
    {
        TextureSetup Setup = (hasOverride) ? *target->m_OverrideMSAASurface : TextureSetup(w, h);
        int Override = (hasOverride) ? -1 : target->m_UseMSAA;

        if (m_Game->m_VRDebuglvl) m_Game->logMsg(LOGTYPE_DEBUG, "CreateRT: %s_MSAA, W: %d, H: %d", name, w, h);
        PushTexture(target, Override);
        target->m_MSAAITex = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx(std::string(name).append("_MSAA").c_str(), Setup.w, Setup.h, sizeMode, format, depth, textureFlags);
    }
}

void VR::PushTexture(SharedTextureHolder* holder, int isMSAA)
{
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    m_TextureQueue.push({ isMSAA, holder });
}

std::pair<int, SharedTextureHolder*> VR::PopNextTexture()
{
    std::lock_guard<std::mutex> lock(m_QueueMutex);

    if (m_TextureQueue.empty())
        return { 0, nullptr };


    auto entry = m_TextureQueue.front();
    m_TextureQueue.pop();
    return entry;
}

void VR::RegisterPanelCaptureRoot(VPANEL panel, ITexture* dest, std::function<bool()> func, std::vector<std::pair<const char*, ITexture*>> ExcludeList)
{
    m_PanelCaptureMap[panel] = { dest, ExcludeList, func};
}

void VR::OverridePanelLayout(std::string TargetLayout, OverrideLayout NewLayout)
{
    m_PanelLayoutOverride[ToLower(TargetLayout)] = NewLayout;
}

void VR::RegisterPanelCommandListener(std::initializer_list<std::string> Commands, std::function<bool(const char* cmd, Panel* panel, KeyValues* message)> func)
{
    for (const auto& command : Commands)
    {
        m_PanelCommands[command] = func;
    }
}

void VR::ModifyPanelSettings(std::string PanelName, std::function<bool(Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& SettingRuntimeData)> func)
{
    m_PanelSettings[PanelName] = { func };
}

void VR::SetBinding(const char* pchActionName, VRBindingType bindingType, const char* pressCommand, const char* releaseCommand, bool holdPress, std::function<void(vr::VRActionHandle_t handle)> func)
{
    std::string path = pchActionName;
    VRBindings Bind = VRBindings(path.substr(path.find_last_of('/') + 1).c_str(), bindingType, pressCommand, releaseCommand, func, holdPress);

    m_Input->GetActionHandle(pchActionName, &Bind.m_Handle);
    m_Bindings.push_back(Bind);
}