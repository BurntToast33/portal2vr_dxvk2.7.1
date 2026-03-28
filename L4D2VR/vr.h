#pragma once
#include "openvr.h"
#include "vector.h"
#include <chrono>
#include <thread>
#include <unordered_map>
#include <functional>
#include <d3d9.h>
#include "sdk.h"


class Game;
class ITexture;


struct TrackedDevicePoseData 
{
	std::string TrackedDeviceName;
	Vector TrackedDevicePos;
	Vector TrackedDeviceVel;
	QAngle TrackedDeviceAng;
	QAngle TrackedDeviceAngVel;
};

struct SharedTextureHolder 
{
	vr::VRVulkanTextureData_t m_VulkanData{};
	vr::Texture_t m_VRTexture{};

	ITexture* m_ITex = nullptr;

	IDirect3DSurface9* m_Surface = nullptr;
	IDirect3DSurface9* m_MSAASurface = nullptr;
	bool m_UseMSAA = false;

	//Optional functionality
	std::function<void(UINT Width, UINT Height, UINT LEVELS, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool)> m_CustomSetup = nullptr;
};

struct PanelCaptureInfo
{
	IDirect3DSurface9* m_Surface = nullptr;
	std::function<bool()> m_ShouldCapture = nullptr;
};

class VR
{
public:
	Game *m_Game = nullptr;

	vr::IVRSystem *m_System = nullptr;
	vr::IVRInput *m_Input = nullptr;
	vr::IVROverlay *m_Overlay = nullptr;

	vr::VROverlayHandle_t m_MainMenuHandle = 0;
	//vr::VROverlayHandle_t m_HUDHandle;

	float m_HorizontalOffsetLeft = 0;
	float m_VerticalOffsetLeft = 0;
	float m_HorizontalOffsetRight = 0;
	float m_VerticalOffsetRight = 0;

	uint32_t m_RenderWidth = 0, m_RenderHeight = 0;
	float m_Aspect = 0;
	float m_Fov = 0;

	vr::VRTextureBounds_t m_TextureBounds[2];
	vr::TrackedDevicePose_t m_Poses[vr::k_unMaxTrackedDeviceCount] = {};

	Vector m_EyeToHeadTransformPosLeft = { 0,0,0 };
	Vector m_EyeToHeadTransformPosRight = { 0,0,0 };

	Vector m_HmdForward;
	Vector m_HmdRight;
	Vector m_HmdUp;

	Vector m_HmdPosLocalInWorld = { 0,0,0 };

	Vector m_LeftControllerForward;
	Vector m_LeftControllerRight;
	Vector m_LeftControllerUp;

	Vector m_RightControllerForward;
	Vector m_RightControllerRight;
	Vector m_RightControllerUp;

	Vector m_ViewmodelForward;
	Vector m_ViewmodelRight;
	Vector m_ViewmodelUp;

	QAngle m_HmdAngAbs;

	Vector m_HmdPosRelativeRaw = { 0,0,0 };
	Vector m_HmdPosRelativeRawPrev = { 0,0,0 };

	Vector m_HmdPosRelative = { 0,0,0 };
	Vector m_HmdPosRelativePrev = { 0,0,0 };

	Vector m_AimPos = { 0, 0, 0 };
	bool m_Traced = false;

	Vector m_Center = { 0,0,0 };
	Vector m_SetupOrigin = { 0,0,0 };

	float m_HeightOffset = 0.0;
	bool m_RoomscaleActive = false;

	Vector m_LeftControllerPosAbs;											
	QAngle m_LeftControllerAngAbs;
	Vector m_RightControllerPosRel;											
	QAngle m_RightControllerAngAbs;

	Vector m_ViewmodelPosOffset;
	QAngle m_ViewmodelAngOffset;

	Vector m_ViewmodelPosCustomOffset; // Custom (from config) viewmodel position offset applied on top of hardcoded ones
    QAngle m_ViewmodelAngCustomOffset; // Custom (from config) viewmodel angle offset applied on top of hardcoded ones

	float m_Ipd = 0;																	
	float m_EyeZ = 0;

	Vector m_IntendedPositionOffset = { 0,0,0 };

	enum TextureID
	{
		Texture_None = 0,

		Texture_LeftEye,
		Texture_RightEye,
		Texture_Blank,
		Texture_Menu,
		
		Texture_Count //Num of textures
	};

	SharedTextureHolder m_LeftEye;
	SharedTextureHolder m_RightEye;
	SharedTextureHolder m_BackBuffer;
	SharedTextureHolder m_BlankTexture;
	SharedTextureHolder m_MenuTexture;

	bool m_IsVREnabled = false;
	bool m_IsInitialized = false;
	bool m_RenderedHud = false;
	bool m_CreatedVRTextures = false;
	bool m_DrawCrosshair = false;
	TextureID m_CreatingTextureID = Texture_None;

	bool m_PressedTurn = false;
	bool m_LastOverlayRepos = false; //False = world space, True = headset space

	float m_WScaleDownRatio, m_HScaleDownRatio, m_WScaleUpRatio, m_HScaleUpRatio;

	std::unordered_map<std::string, std::string> m_BackgroundMapping{};
	std::unordered_map<VR::TextureID, SharedTextureHolder*> m_TextureMap{};
	std::unordered_map<VPANEL, PanelCaptureInfo> m_PanelCaptureMap{};
	bool m_BuiltCaptureMap = false;
	bool m_IsLevelBackground = false;
	bool m_StopLoading3DBgr = false;
	bool m_IsCredits = false;

	bool m_DrawingViewModel = false;

	// action set
	vr::VRActionSetHandle_t m_ActionSet = {};
	vr::VRActiveActionSet_t m_ActiveActionSet = {};

	// actions
	vr::VRActionHandle_t m_ActionJump = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionPrimaryAttack = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionSecondaryAttack = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionReload = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionWalk = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionTurn = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionUse = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionNextItem = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionPrevItem = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionResetPosition = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionCrouch = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionFlashlight = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ActionActivateVR = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_MenuSelect = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_MenuBack = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_MenuUp = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_MenuDown = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_MenuLeft = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_MenuRight = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_Spray = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_Scoreboard = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_ShowHUD = vr::k_ulInvalidActionHandle;
	vr::VRActionHandle_t m_Pause = vr::k_ulInvalidActionHandle;

	TrackedDevicePoseData m_HmdPose;
	TrackedDevicePoseData m_LeftControllerPose;
	TrackedDevicePoseData m_RightControllerPose;

	bool m_ApplyPortalRotationOffset = false;
	QAngle m_PortalRotationOffset = {0, 0, 0};
	QAngle m_RotationOffset = { 0, 0, 0 };
	bool m_OverrideEyeAngles = false;
	std::chrono::steady_clock::time_point m_PrevFrameTime;

	//Settings
	float m_TurnSpeed = 0.15;
	bool m_SnapTurning = false;
	float m_SnapTurnAngle = 45.0;
	bool m_LeftHanded = false;
	float m_VRScale = 43.2;
	float m_IpdScale = 1.0;
	bool m_6DOF = true;
	float m_HudDistance = 1.3;
	float m_HudSize = 4.0;
	bool m_HudAlwaysVisible = false;
	int m_AimMode = 2;
	bool m_3DMenu = false;
	bool m_RenderWindow = false;
	uint32_t m_AntiAliasing = 0;


	uint64_t m_SteamID = 0; //Used to know the exact directory to find the save files


	VR() {};
	VR(Game *game);
	~VR();
	void CreateHashMaps();
	int SetActionManifest(const char *fileName);
	void InstallApplicationManifest(const char *fileName);
	void PreUpdate();
	void PostUpdate();
	void FirstFrameUpdate();
	void SetScreenSizeOverride(bool bState);
	void CreateVRTextures();
	void SubmitVRTextures();
	void RepositionOverlays();
	void GetPoses();
	void UpdatePosesAndActions();
	void GetViewParameters();
	void ProcessMenuInput();
	void ProcessInput();
	VMatrix VMatrixFromHmdMatrix(const vr::HmdMatrix34_t &hmdMat);
	vr::HmdMatrix34_t VMatrixToHmdMatrix(const VMatrix &vMat);
	vr::HmdMatrix34_t GetControllerTipMatrix(vr::ETrackedControllerRole controllerRole);
	bool CheckOverlayIntersectionForController(vr::VROverlayHandle_t overlayHandle, vr::ETrackedControllerRole controllerRole);
	QAngle GetRightControllerAbsAngle();
	QAngle& GetRightControllerAbsAngleConst();
	Vector GetRightControllerAbsPos(Vector eyePosition = {0, 0, 0});
	Vector GetRecommendedViewmodelAbsPos(Vector eyePosition);
	QAngle GetRecommendedViewmodelAbsAngle();
	void UpdateHMDAngles();
	void UpdateTracking();
	Vector GetViewAngle();
	Vector GetViewOrigin(Vector setupOrigin);
	Vector GetViewOriginLeft(Vector setupOrigin);
	Vector GetViewOriginRight(Vector setupOrigin);
	bool PressedDigitalAction(vr::VRActionHandle_t &actionHandle, bool checkIfActionChanged = false);
	bool GetAnalogActionData(vr::VRActionHandle_t &actionHandle, vr::InputAnalogActionData_t &analogDataOut);
	void ResetPosition();
	void GetPoseData(const vr::TrackedDevicePose_t &poseRaw, TrackedDevicePoseData &poseOut);
	void ParseConfigFile();
	void WaitForConfigUpdate();
	Vector Trace(uint32_t* localPlayer);
	Vector TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle);
	void Load3DMenu();
	std::string GetMapFromSave(const char* fileName);
	std::string GetNewestPortal2SavePath(const std::string& baseDir);
	bool ShouldCapture();
	void BuildCaptureMap();
};