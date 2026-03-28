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
#define GetControllerTipMatrix_HeapBuffer //Moves some memory allocation of function to heap because it was larger then 32kb


VR::VR(Game *game) 
{
    m_Game = game;
    char errorString[MAX_STR_LEN];

    vr::HmdError error = vr::VRInitError_None;
    m_System = vr::VR_Init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None) 
    {
        snprintf(errorString, MAX_STR_LEN, "VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
        Game::errorMsg(errorString);
        return;
    }

    vr::EVRInitError peError = vr::VRInitError_None;

    if (!vr::VRCompositor())
    {
        Game::errorMsg("Compositor initialization failed.");
        return;
    }

    m_Input = vr::VRInput();
    m_System = vr::OpenVRInternal_ModuleContext().VRSystem();

    m_System->GetRecommendedRenderTargetSize(&m_RenderWidth, &m_RenderHeight);

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
    m_Overlay->CreateOverlay("MenuOverlayKey", "MenuOverlay", &m_MainMenuHandle);
    //m_Overlay->CreateOverlay("HUDOverlayKey", "HUDOverlay", &m_HUDHandle);
    m_Overlay->SetOverlayInputMethod(m_MainMenuHandle, vr::VROverlayInputMethod_Mouse);
   // m_Overlay->SetOverlayInputMethod(m_HUDHandle, vr::VROverlayInputMethod_Mouse);
    m_Overlay->SetOverlayFlag(m_MainMenuHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
    //m_Overlay->SetOverlayFlag(m_HUDHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

    //const vr::HmdVector2_t mouseScaleHUD = {windowWidth, windowHeight};
    //m_Overlay->SetOverlayMouseScale(m_HUDHandle, &mouseScaleHUD);

    const vr::HmdVector2_t mouseScaleMenu = {m_RenderWidth, m_RenderHeight};
    m_Overlay->SetOverlayCurvature(m_MainMenuHandle, 0.15f);
    m_Overlay->SetOverlayMouseScale(m_MainMenuHandle, &mouseScaleMenu);

    UpdatePosesAndActions();

    m_SteamID = GetSteamID64();
    m_IsInitialized = true;
    m_IsVREnabled = true;
}

VR::~VR()
{
    m_IsInitialized = false;
    m_IsVREnabled = false;

    for (size_t I = 1; I < Texture_Count; I++) 
    {
        auto it = m_TextureMap.find((TextureID)I);
        if (it == m_TextureMap.end())
        {
            g_Game->logMsg(LOGTYPE_WARNING, "VR Cleanup: SharedTextureHolder %d doesn't exist", I);
            continue;
        }
            
        if (it->second->m_MSAASurface) { it->second->m_MSAASurface->Release(); }
    }
}

//Creates the hash maps used to later
void VR::CreateHashMaps()
{
    //Texture mappings
    m_LeftEye.m_UseMSAA = m_AntiAliasing;
    m_RightEye.m_UseMSAA = m_AntiAliasing;
    m_MenuTexture.m_CustomSetup = [this](UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool)
    {
        IDirect3DTexture9* temp = nullptr;

        this->m_CreatingTextureID = VR::Texture_None;
        this->m_Game->m_DxDevice->CreateTexture(
            this->m_Game->m_WindowWidth, this->m_Game->m_WindowHeight, Levels, Usage, Format, Pool, &temp, nullptr);

        temp->GetSurfaceLevel(0, &this->m_MenuTexture.m_MSAASurface);
        temp->Release();
    }; //Using msaa surface as the lower resolution target

    m_TextureMap = {
        { VR::Texture_LeftEye, &m_LeftEye},
        { VR::Texture_RightEye, &m_RightEye},
        { VR::Texture_Blank, &m_BlankTexture },
        { VR::Texture_Menu, &m_MenuTexture },
    };

    m_Game->logMsg(LOGTYPE_DEBUG, "Loaded texture mappings.");


    //Background mappings
    char path[MAX_STR_LEN]{};
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\backgrounds.txt", m_Game->m_GameDir);

    std::ifstream file(path);
    if (!file.is_open())
    {
        m_Game->logMsg(LOGTYPE_WARNING, "Failed to open: %s%s", path, "\nDisabling 3D backgrounds");
        m_3DMenu = false;
        return;
    }

    m_BackgroundMapping.clear();

    std::string line;
    while (std::getline(file, line))
    {
        //Trimming whitespace before and after data entry
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        //Skipping empty lines or comments
        if (line.empty() || line[0] == '#')
            continue;

        //Finding middle point
        size_t MiddlePos = line.find('=');
        if (MiddlePos == std::string::npos)
            continue;

        //Getting data
        std::string chapter = line.substr(0, MiddlePos);
        std::string background = line.substr(MiddlePos + 1);

        //Trimming redundant white space of data
        chapter.erase(0, chapter.find_first_not_of(" \t\r\n"));
        chapter.erase(chapter.find_last_not_of(" \t\r\n") + 1);

        background.erase(0, background.find_first_not_of(" \t\r\n"));
        background.erase(background.find_last_not_of(" \t\r\n") + 1);

        if (!chapter.empty() && !background.empty())
            m_BackgroundMapping[chapter] = background;
    }

    file.close();

    if (m_BackgroundMapping.size())
    {
        m_Game->logMsg(LOGTYPE_DEBUG, "Loaded %zu background mappings.", m_BackgroundMapping.size());
    }
    else
    {
        m_Game->logMsg(LOGTYPE_WARNING, "Failed to parse backgrounds.txt or there are no entries.%s", "\nDisabling 3D backgrounds");
        m_3DMenu = false;
    }
}

//Binds actions to variables
int VR::SetActionManifest(const char *fileName) 
{
    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\SteamVRActionManifest\\%s", m_Game->m_GameDir, fileName);

    if (m_Input->SetActionManifestPath(path) != vr::VRInputError_None) 
    {
        Game::errorMsg("SetActionManifestPath failed");
    }

    m_Input->GetActionHandle("/actions/main/in/ActivateVR", &m_ActionActivateVR);
    m_Input->GetActionHandle("/actions/main/in/Jump", &m_ActionJump);
    m_Input->GetActionHandle("/actions/main/in/PrimaryAttack", &m_ActionPrimaryAttack);
    m_Input->GetActionHandle("/actions/main/in/Reload", &m_ActionReload);
    m_Input->GetActionHandle("/actions/main/in/Use", &m_ActionUse);
    m_Input->GetActionHandle("/actions/main/in/Walk", &m_ActionWalk);
    m_Input->GetActionHandle("/actions/main/in/Turn", &m_ActionTurn);
    m_Input->GetActionHandle("/actions/main/in/SecondaryAttack", &m_ActionSecondaryAttack);
    m_Input->GetActionHandle("/actions/main/in/NextItem", &m_ActionNextItem);
    m_Input->GetActionHandle("/actions/main/in/PrevItem", &m_ActionPrevItem);
    m_Input->GetActionHandle("/actions/main/in/ResetPosition", &m_ActionResetPosition);
    m_Input->GetActionHandle("/actions/main/in/Crouch", &m_ActionCrouch);
    m_Input->GetActionHandle("/actions/main/in/Flashlight", &m_ActionFlashlight);
    m_Input->GetActionHandle("/actions/main/in/MenuSelect", &m_MenuSelect);
    m_Input->GetActionHandle("/actions/main/in/MenuBack", &m_MenuBack);
    m_Input->GetActionHandle("/actions/main/in/MenuUp", &m_MenuUp);
    m_Input->GetActionHandle("/actions/main/in/MenuDown", &m_MenuDown);
    m_Input->GetActionHandle("/actions/main/in/MenuLeft", &m_MenuLeft);
    m_Input->GetActionHandle("/actions/main/in/MenuRight", &m_MenuRight);
    m_Input->GetActionHandle("/actions/main/in/Spray", &m_Spray);
    m_Input->GetActionHandle("/actions/main/in/Scoreboard", &m_Scoreboard);
    m_Input->GetActionHandle("/actions/main/in/ShowHUD", &m_ShowHUD);
    m_Input->GetActionHandle("/actions/main/in/Pause", &m_Pause);

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

void VR::SetScreenSizeOverride(bool bState) {
    bool isOverriding = m_Game->m_VguiSurface->IsScreenSizeOverrideActive();

    if (bState && !isOverriding || !bState && isOverriding) {
        int iOldWidth, iOldHeight;
        m_Game->m_VguiSurface->GetScreenSize(iOldWidth, iOldHeight);
        m_Game->m_VguiSurface->ForceScreenSizeOverride(bState, m_RenderWidth, m_RenderHeight);
       /*int x = 0, y = 0, w = m_RenderWidth, h = m_RenderHeight;

        if (m_Game->m_ClientMode->GetViewport())
            m_Game->m_ClientMode->AdjustEngineViewport(x, y, w, h);*/

        if (bState) {
            /*IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
            renderContext->Viewport(0, 0, m_RenderWidth, m_RenderHeight);
            renderContext->Release();*/
        }

        m_Game->m_VguiSurface->OnScreenSizeChanged(iOldWidth, iOldHeight);
    }
}

void VR::PreUpdate()
{
    //Compositing the rendered frame to the back buffer
    if (m_RenderWindow && m_Game->m_EngineClient->IsInGame())
    {
        m_Game->m_DxDevice->StretchRect(
            m_RightEye.m_Surface, nullptr,
            m_BackBuffer.m_Surface, nullptr,
            D3DTEXF_NONE
        );
    }

    //Scaling Menu textures
    if (ShouldCapture()) 
    {
        m_Game->m_DxDevice->StretchRect(
            m_MenuTexture.m_MSAASurface, nullptr,
            m_MenuTexture.m_Surface, nullptr,
            D3DTEXF_LINEAR
        );
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
        //m_CreatedVRTextures = false; // Have to recreate textures otherwise some workshop maps won't render

        Load3DMenu();
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
    if (m_Game->m_EngineClient->IsInGame())
    {
        m_Game->m_DxDevice->SetRenderTarget(0, m_MenuTexture.m_MSAASurface);
        m_Game->m_DxDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
    }
    else
        m_Game->m_DxDevice->SetRenderTarget(0, m_BackBuffer.m_Surface);
}

void VR::FirstFrameUpdate()
{
    m_Game->ClientCmd_Unrestricted("mat_motion_blur_enabled 0");

    if (m_Game->m_EngineClient->IsPaused())
        m_Game->ClientCmd_Unrestricted("pause");

    ResetPosition();
}

//Creates the target textures on the engine side to get picked up on dxvk side
void VR::CreateVRTextures()
{
    m_Game->m_MaterialSystem->isGameRunning = false;
    m_Game->m_MaterialSystem->BeginRenderTargetAllocation();
    m_Game->m_MaterialSystem->isGameRunning = true;

    //Blank texture gets created when the game is loading
    if (!m_BlankTexture.m_ITex)
    {
        m_CreatingTextureID = Texture_Blank;
        m_BlankTexture.m_ITex = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("blankTexture", 512, 512, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);
        m_CreatingTextureID = Texture_None;
    }
    if (!m_Game->m_EngineClient->IsInGame())
    {
        m_Game->m_MaterialSystem->EndRenderTargetAllocation();
        return;
    }


    //Textures will be recreated every map load
    m_Game->logMsg(LOGTYPE_DEBUG, "RenderTexture - Width: %d, Height: %d", m_RenderWidth, m_RenderHeight);
    if (m_LeftEye.m_ITex) m_LeftEye.m_ITex->Release();

    m_CreatingTextureID = Texture_LeftEye;
    m_LeftEye.m_ITex = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("leftEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);

    if (m_RightEye.m_ITex) m_RightEye.m_ITex->Release();

    m_CreatingTextureID = Texture_RightEye;
    m_RightEye.m_ITex = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("rightEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);
    
    if (m_MenuTexture.m_ITex) m_MenuTexture.m_ITex->Release();

    m_CreatingTextureID = Texture_Menu;
    m_MenuTexture.m_ITex = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("menuTex", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);

    m_CreatingTextureID = Texture_None;
    m_Game->m_MaterialSystem->EndRenderTargetAllocation();

    m_CreatedVRTextures = true;
}

//Submits vr textures and handles menus
void VR::SubmitVRTextures()
{
    if (!m_CreatedVRTextures)
        CreateVRTextures();

    //2D mode
    if (!g_Game->m_EngineClient->IsInGame())
    {
        if (!m_Overlay->IsOverlayVisible(m_MainMenuHandle) || m_LastOverlayRepos)
        {
            RepositionOverlays();
            vr::VRTextureBounds_t bounds{ 0, 0, 1, 1 };
            m_Overlay->SetOverlayTextureBounds(m_MainMenuHandle, &bounds);
            vr::VROverlay()->SetOverlayTexelAspect(m_MainMenuHandle, 1.0f);
            m_LastOverlayRepos = false;
        }
            
        m_Overlay->SetOverlayTexture(m_MainMenuHandle, &m_BackBuffer.m_VRTexture);
        m_Overlay->ShowOverlay(m_MainMenuHandle);

        vr::VRCompositor()->Submit(vr::Eye_Left, &m_BlankTexture.m_VRTexture, nullptr, vr::Submit_Default);
        vr::VRCompositor()->Submit(vr::Eye_Right, &m_BlankTexture.m_VRTexture, nullptr, vr::Submit_Default);
        return;
    }

    //3D mode
    if (ShouldCapture())
    {
        if (!m_Overlay->IsOverlayVisible(m_MainMenuHandle) || !m_LastOverlayRepos)
        {
            RepositionOverlays();
            m_LastOverlayRepos = true;
        }

        m_Overlay->SetOverlayTexture(m_MainMenuHandle, &m_MenuTexture.m_VRTexture);
        m_Overlay->ShowOverlay(m_MainMenuHandle);
    }
    else m_Overlay->HideOverlay(m_MainMenuHandle);
    
    vr::VRCompositor()->Submit(vr::Eye_Left, &m_LeftEye.m_VRTexture, &(m_TextureBounds)[0], vr::Submit_Default);
    vr::VRCompositor()->Submit(vr::Eye_Right, &m_RightEye.m_VRTexture, &(m_TextureBounds)[1], vr::Submit_Default);
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

void VR::RepositionOverlays()
{
    vr::TrackedDevicePose_t hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];
    vr::HmdMatrix34_t hmdMat = hmdPose.mDeviceToAbsoluteTracking;
    Vector hmdPosition = { hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3] };
    Vector hmdForward = { -hmdMat.m[0][2], 0, -hmdMat.m[2][2] };
    VectorNormalize(hmdForward);

    vr::HmdMatrix34_t menuTransform =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f
    };

    vr::ETrackingUniverseOrigin trackingOrigin = vr::VRCompositor()->GetTrackingSpace();

    menuTransform.m[0][0] *= m_WScaleDownRatio;
    menuTransform.m[1][1] *= m_HScaleDownRatio;

    if (m_Game->m_EngineClient->IsInGame())
    {
        //Headspace overlay
        Vector menuDistance = hmdForward * 3.0f;
        Vector menuNewPos = hmdPosition + menuDistance;

        menuTransform.m[0][3] = menuNewPos.x;
        menuTransform.m[1][3] = menuNewPos.y - 0.25f;
        menuTransform.m[2][3] = menuNewPos.z;

        float xScale = menuTransform.m[0][0];
        float hmdRotationDegrees = atan2f(hmdMat.m[0][2], hmdMat.m[2][2]);

        menuTransform.m[0][0] *= cos(hmdRotationDegrees);
        menuTransform.m[0][2] = sin(hmdRotationDegrees);
        menuTransform.m[2][0] = -sin(hmdRotationDegrees) * xScale;
        menuTransform.m[2][2] *= cos(hmdRotationDegrees);
    }
    else
    {
        //World space
        Vector worldCenter = { 0.0f, 0.0f, 0.0f };

        Vector vrForward = { 0.0f, 0.0f, -1.0f };
        Vector overlayPos = worldCenter + vrForward * 3.0f; // Forward Distance
        overlayPos.y = 2.0f; // Height

        Vector forward = worldCenter - overlayPos;
        forward.y = 0.0f;
        VectorNormalize(forward);

        float yaw = atan2f(forward.x, forward.z);
        float cosYaw = cosf(yaw);
        float sinYaw = sinf(yaw);

        menuTransform.m[0][0] = cosYaw * m_WScaleDownRatio;
        menuTransform.m[0][1] = 0.0f;
        menuTransform.m[0][2] = -sinYaw;
        menuTransform.m[0][3] = overlayPos.x;

        menuTransform.m[1][0] = 0.0f;
        menuTransform.m[1][1] = 1.0f * m_HScaleDownRatio;
        menuTransform.m[1][2] = 0.0f;
        menuTransform.m[1][3] = overlayPos.y;

        menuTransform.m[2][0] = sinYaw;
        menuTransform.m[2][1] = 0.0f;
        menuTransform.m[2][2] = cosYaw;
        menuTransform.m[2][3] = overlayPos.z;
    }

    m_Overlay->SetOverlayTransformAbsolute(m_MainMenuHandle, trackingOrigin, &menuTransform);
    m_Overlay->SetOverlayWidthInMeters(m_MainMenuHandle, 1.5f * (1.0f / m_HScaleDownRatio));
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
    //vr::VROverlayHandle_t currentOverlay = m_Game->m_EngineClient->IsInGame() ? m_HUDHandle : m_MainMenuHandle;
    vr::VROverlayHandle_t currentOverlay = m_MainMenuHandle;

    // Check if left or right hand controller is pointing at the overlay
    const bool isHoveringOverlay = CheckOverlayIntersectionForController(currentOverlay, vr::TrackedControllerRole_LeftHand) ||
                                   CheckOverlayIntersectionForController(currentOverlay, vr::TrackedControllerRole_RightHand);

    // Overlays can't process action inputs if the laser is active, so
    // only activate laser if a controller is pointing at the overlay
    if (isHoveringOverlay)
    {
        m_Overlay->SetOverlayFlag(currentOverlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);

        vr::VREvent_t vrEvent;
        while (m_Overlay->PollNextOverlayEvent(currentOverlay, &vrEvent, sizeof(vrEvent)))
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
                // Don't allow holding down the mouse down in the pause menu. The resume button can be clicked before
                // the MouseButtonUp event is polled, which causes issues with the overlay.
                if (currentOverlay == m_MainMenuHandle)
                {
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                }
                break;

            case vr::VREvent_MouseButtonUp:
                /*if (currentOverlay == m_HUDHandle)
                {
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                }*/
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));
                break;

            case vr::VREvent_ScrollDiscrete:
                m_Game->m_VguiInput->InternalMouseWheeled((int)vrEvent.data.scroll.ydelta);
                break;
            }
        }
    }
    else
    {
        m_Overlay->SetOverlayFlag(currentOverlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
        
        if (PressedDigitalAction(m_MenuSelect, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuBack, true) || PressedDigitalAction(m_Pause, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_ESCAPE;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuUp, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_UP;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuDown, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_DOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuLeft, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_LEFT;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuRight, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RIGHT;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
    }
}

void VR::ProcessInput()
{
    if (!m_IsVREnabled)
        return;

    //vr::VROverlay()->SetOverlayFlag(m_HUDHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);

    vr::InputAnalogActionData_t analogActionData;

    if (GetAnalogActionData(m_ActionTurn, analogActionData))
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

    if (PressedDigitalAction(m_ActionPrimaryAttack))
    {
        m_Game->ClientCmd_Unrestricted("+attack");
    }
    else
    {
        m_Game->ClientCmd_Unrestricted("-attack");
    }

    if (PressedDigitalAction(m_ActionSecondaryAttack))
    {
        m_Game->ClientCmd_Unrestricted("+attack2");
    }
    else
    {
        m_Game->ClientCmd_Unrestricted("-attack2");
    }

    if (PressedDigitalAction(m_ActionJump))
    {
        m_Game->ClientCmd_Unrestricted("+jump");
    }
    else
    {
        m_Game->ClientCmd_Unrestricted("-jump");
    }

    if (PressedDigitalAction(m_ActionCrouch))
    {
        m_Game->ClientCmd_Unrestricted("+duck");
    }
    else
    {
        m_Game->ClientCmd_Unrestricted("-duck");
    }

    if (PressedDigitalAction(m_ActionUse))
    {
        m_Game->ClientCmd_Unrestricted("+use");
    }
    else
    {
        m_Game->ClientCmd_Unrestricted("-use");
    }

    if (PressedDigitalAction(m_ActionReload))
    {
        m_Game->ClientCmd_Unrestricted("+reload");
    }
    else
    {
        m_Game->ClientCmd_Unrestricted("-reload");
    }

    if (PressedDigitalAction(m_ActionPrevItem, true))
    {
        m_Game->ClientCmd_Unrestricted("invprev");
    }
    else if (PressedDigitalAction(m_ActionNextItem, true))
    {
        m_Game->ClientCmd_Unrestricted("invnext");
    }

    if (PressedDigitalAction(m_ActionResetPosition, true))
    {
        ResetPosition();
    }

    if (PressedDigitalAction(m_ActionFlashlight, true))
    {
        m_Game->ClientCmd_Unrestricted("impulse 100");
    }

    if (PressedDigitalAction(m_Spray, true))
    {
        m_Game->ClientCmd_Unrestricted("impulse 201");
    }
    
    /*bool isControllerVertical = m_RightControllerAngAbs.x > 60 || m_RightControllerAngAbs.x < -45;
    if ((PressedDigitalAction(m_ShowHUD) || PressedDigitalAction(m_Scoreboard) || isControllerVertical || m_HudAlwaysVisible)
        && m_RenderedHud)
    {
        if (!vr::VROverlay()->IsOverlayVisible(m_HUDHandle) || m_HudAlwaysVisible)
            RepositionOverlays();

        if (PressedDigitalAction(m_Scoreboard))
            m_Game->ClientCmd_Unrestricted("+showscores");
        else
            m_Game->ClientCmd_Unrestricted("-showscores");

        vr::VROverlay()->ShowOverlay(m_HUDHandle);
    }
    else
    {
        vr::VROverlay()->HideOverlay(m_HUDHandle);
    }*/

    m_RenderedHud = false;

    if (PressedDigitalAction(m_Pause, true))
    {
        m_Game->ClientCmd_Unrestricted("gameui_activate");
        RepositionOverlays();
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

        if (activeWeapon && m_DrawCrosshair) {
            if (portalPlayer->m_PointLaser) {
                portalPlayer->m_PointLaser->SetControlPoint(1, m_AimPos);
                portalPlayer->m_PointLaser->SetControlPoint(2, m_Game->m_singlePlayerPortalColors[activeWeapon->m_iLastFiredPortal] * 0.5f);
            }
            else {
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
static T parseConfigEntry(
    const std::unordered_map<std::string, std::string>& userConfig, Game& game,
    const char* key, const T& defaultValue)
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
    const auto parseOrDefault = [&](const char* key, auto& target,
                                    const auto& defaultValue) 
    { 
        target = parseConfigEntry(userConfig, *m_Game, key, defaultValue);
        m_Game->logMsg(LOGTYPE_DEBUG, "Setting %s to %s", key, std::to_string(target).c_str());
    };

    // Parses a vector or angle from the config into 'target'. The XYZ coordinates
    // are read from three separate config entries with key 'keyPrefix' + 'X'/'Y'/'Z'.
    // If any entry does not exist, or if the parsing fails, sets the corresponding
    // coordinate in 'target' to zero.
    const auto parseXYZOrDefaultZero = [&](std::string keyPrefix, auto& target)
    {
        parseOrDefault((keyPrefix + "X").c_str(), target.x, 0.f);
        parseOrDefault((keyPrefix + "Y").c_str(), target.y, 0.f);
        parseOrDefault((keyPrefix + "Z").c_str(), target.z, 0.f);
    };

    parseOrDefault("SnapTurning", m_SnapTurning, false);
    parseOrDefault("SnapTurnAngle", m_SnapTurnAngle, 45.0f);
    parseOrDefault("TurnSpeed", m_TurnSpeed, 0.15f);
    parseOrDefault("LeftHanded", m_LeftHanded, false);
    parseOrDefault("VRScale", m_VRScale, 43.2f);
    parseOrDefault("IPDScale", m_IpdScale, 1.0f);
    parseOrDefault("6DOF", m_6DOF, true);
    /*parseOrDefault("HudDistance", m_HudDistance, 1.3f);
    parseOrDefault("HudSize", m_HudSize, 4.0f);
    parseOrDefault("HudAlwaysVisible", m_HudAlwaysVisible, false);*/
    parseOrDefault("AimMode", m_AimMode, 2);
    parseOrDefault("AntiAliasing", m_AntiAliasing, 0);
    parseOrDefault("RenderWindow", m_RenderWindow, false);
    parseOrDefault("Enable3DBackground", m_3DMenu, false);
    parseXYZOrDefaultZero("ViewmodelPosCustomOffset", m_ViewmodelPosCustomOffset);
    parseXYZOrDefaultZero("ViewmodelAngCustomOffset", m_ViewmodelAngCustomOffset);
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
void VR::Load3DMenu() 
{
    if (!m_3DMenu || m_StopLoading3DBgr || m_Game->m_EngineClient->IsInGame() || m_Game->m_EngineClient->IsDrawingLoadingImage())
    {
        m_StopLoading3DBgr = true;
        return;
    }

    std::string SaveFile = GetNewestPortal2SavePath(m_Game->m_GameDir);
    if (SaveFile.empty())
    {
        m_StopLoading3DBgr = true;
        return;
    }

    std::string SaveMap = GetMapFromSave(SaveFile.c_str());
    if (SaveMap.empty())
    {
        m_StopLoading3DBgr = true;
        return;
    }

    std::string backgroundMap;
    auto it = m_BackgroundMapping.find(SaveMap);
    if (it == m_BackgroundMapping.end())
    {
        m_Game->logMsg(LOGTYPE_WARNING, "%s is not mapped to any backgrounds.", SaveMap.c_str());
        m_StopLoading3DBgr = true;
        return;
    }
        
    backgroundMap = it->second;
    m_Game->logMsg(LOGTYPE_DEBUG, "Loading background: %s", backgroundMap.c_str());
    m_Game->m_EngineClient->ClientCmd_Unrestricted(("map_background " + backgroundMap).c_str());

    m_StopLoading3DBgr = true;
}

bool VR::ShouldCapture()
{
    return m_IsLevelBackground || m_IsCredits || m_Game->m_EngineClient->IsPaused();
}

void VR::BuildCaptureMap()
{
    m_PanelCaptureMap = {
        {
            m_Game->m_EnginePanel->GetPanel(PANEL_GAMEUIDLL),
            PanelCaptureInfo{ m_MenuTexture.m_MSAASurface, [this]() { return this->ShouldCapture(); } }
        }
    };

    m_BuiltCaptureMap = true;
}