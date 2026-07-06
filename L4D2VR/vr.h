#pragma once
#include "openvr.h"
#include "vector.h"
#include <chrono>
#include <thread>
#include <unordered_map>
#include <functional>
#include <optional>
#include <variant>
#include <d3d9.h>
#include "sdk.h"
#include "util.h"

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

struct TextureSetup
{
	TextureSetup(int w, int h)
	{
		this->w = w;
		this->h = h;
	}

	int w = 0;
	int h = 0;
};

struct SharedTextureHolder 
{
	vr::VRVulkanTextureData_t m_VulkanData{};
	vr::Texture_t m_VRTexture{};

	//Engine handles
	ITexture* m_ITex = nullptr;
	ITexture* m_MSAAITex = nullptr;

	//Dxvk images needed for resolving MSAA
	dxvk::Rc<dxvk::DxvkImage> m_SurfaceImage = nullptr;
	dxvk::Rc<dxvk::DxvkImage> m_MSAASurfaceImage = nullptr;

	//Dx9 handles
	IDirect3DSurface9* m_Surface = nullptr;
	IDirect3DTexture9* m_Texture = nullptr;
	IDirect3DSurface9* m_MSAASurface = nullptr;
	IDirect3DTexture9* m_MSAATexture = nullptr;

	bool m_UseMSAA = false; //Will be ignored if m_OverrideMSAASurface is set
	std::optional<TextureSetup> m_OverrideMSAASurface; //Creates texture without any msaa sampling
};

struct PanelCaptureInfo
{
	ITexture* m_ITex = nullptr;

	//Panel excluded by name, excluded panels with an ITexture dest will be redirected to that texture
	std::vector<std::pair<const char*, ITexture*>> m_ExcludePanel;
	std::function<bool()> m_ShouldCapture = nullptr; //Condition to capture on
};

struct Overlay
{
	vr::VROverlayHandle_t m_Handle = 0;
	int m_StateFlag = 0; //Can be used for anything
	bool m_Visible = false;
	vr::VROverlayInputMethod m_InputMethod = vr::VROverlayInputMethod_None;
	vr::VROverlayInputMethod m_SaveStateMethod = vr::VROverlayInputMethod_None;

	void ShowOverlay() 
	{
		if (!m_Visible) 
		{
			vr::VROverlay()->ShowOverlay(m_Handle);
			m_Visible = true;
		}
	}

	void HideOverlay()
	{
		if (m_Visible) 
		{
			vr::VROverlay()->HideOverlay(m_Handle);
			m_Visible = false;
		}
	}

	//SaveState saves the last state before changing
	void SetOverlayInputMethod(vr::VROverlayInputMethod method, bool SaveState = false)
	{
		if (m_InputMethod != method)
		{
			vr::VROverlay()->SetOverlayInputMethod(m_Handle, method);

			if (SaveState)
				m_SaveStateMethod = m_InputMethod;

			m_InputMethod = method;
		}
	}
};

struct OverrideLayout
{
	std::string NewLayoutPath;
	std::function<bool(std::string&)> m_Func = [](std::string& LayoutPath){ return true; }; //Condition to capture on
};

struct PanelSettings
{
	std::function<bool(Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& settingData)> m_Func;
	std::unordered_map<std::string, std::variant<bool, float, int>> m_Data = {};

	template <typename T>
	T GetData(const std::string& key)
	{
		auto it = m_Data.find(key);
		if (it == m_Data.end())
		{
			g_Game->logMsg(LOGTYPE_WARNING, "%s not found in PanelSettings", key.c_str());
			return T{};
		}

		if (auto val = std::get_if<T>(&it->second))
			return *val;

		g_Game->logMsg(LOGTYPE_WARNING, "%s type mismatch in PanelSettings", key.c_str());
		return T{};
	}
};

class VR
{
public:
	Game *m_Game = nullptr;

	vr::IVRSystem *m_System = nullptr;
	vr::IVRInput *m_Input = nullptr;
	vr::IVROverlay *m_Overlay = nullptr;

	Overlay m_MainOverlay;
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

	SharedTextureHolder m_LeftEye;
	SharedTextureHolder m_RightEye;
	SharedTextureHolder m_BackBuffer;
	SharedTextureHolder m_BlankTexture;
	SharedTextureHolder m_MenuTexture;
	SharedTextureHolder m_HudTexture;

	bool m_IsVREnabled = false;
	bool m_IsInitialized = false;
	bool m_CreatedVRTextures = false;
	bool m_PressedTurn = false;
	bool m_IsRightEye = false;
	bool m_ParticleCreated = false;

	std::queue<std::pair<int, SharedTextureHolder*>> m_TextureQueue; //Deals with any differences between call and action
	std::mutex m_QueueMutex;

	float m_WScaleDownRatio, m_HScaleDownRatio, m_WScaleUpRatio, m_HScaleUpRatio;

	std::unordered_map<std::string, std::string> m_BackgroundMapping{}; //Map map names to background names

	std::unordered_map<VPANEL, PanelCaptureInfo> m_PanelCaptureMap{}; //Captures the children of the parent panel
	std::unordered_map<std::string, OverrideLayout> m_PanelLayoutOverride{}; //Override layout for loaded panels
	std::unordered_map<std::string, std::function<bool(const char* cmd, Panel* panel, KeyValues* message)>> m_PanelCommands{}; //Listen or override commands from panels
	std::unordered_map<std::string, PanelSettings> m_PanelSettings{}; //Used to modify panel settings

	std::unordered_map<std::string, std::function<void(Panel* panel, float Percentage)>> m_SlideRead;

	bool m_BuiltCaptureMap = false;
	bool m_IsLevelBackground = false;
	bool m_IsCredits = false;
	bool m_OverrideControllerUI = false;
	bool m_LevelExitFix = false; //Disconnects from level before exiting level to fix soft lock
	bool m_3DMenuLoading = false;

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
	int m_ExperimentalOptimizations = 0;
	uint32_t m_AntiAliasing = 0;

	uint64_t m_SteamID = 0; //Used to know the exact directory to find the save files


	//Helpers
	std::string ToLower(std::string str)
	{
		std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
		return str;
	}

	std::string ToLower(const char* input)
	{
		return ToLower(std::string(input));
	}

	//Due to valve jank the slider controls percentage needs to be inversed to work correctly
	float MinMaxInverse(float val, float min, float max)
	{
		return max - (val - min);
	}

	//Due to valve jank traversing up the tree can run into missing parents so we got to traverse down to find a parent
	VPANEL FindParentOf(VPANEL start, const char* target)
	{
		int count = m_Game->m_VguiIPanel->GetChildCount(start);

		for (int i = 0; i < count; i++)
		{
			VPANEL child = m_Game->m_VguiIPanel->GetChild(start, i);

			if (!child)
				continue;

			if (m_Game->m_VguiIPanel->GetName(child) &&
				!strcmp(m_Game->m_VguiIPanel->GetName(child), target))
			{
				return m_Game->m_VguiIPanel->GetParent(child);
			}

			VPANEL result = FindParentOf(child, target);
			if (result)
				return result;
		}

		return 0;
	}

	enum CaptureConditions
	{
		Capture_Any,
		Capture_2D,
		Capture_MenuUI,
		Capture_HudUI
	};

	enum OverlayRelitive
	{
		//Offset is interpreted directly in tracking / world space.
		//No reference device required.
		OverlayRel_WorldSpace,

		//Overlay follows the reference device's position.
		//Offset is interpreted in tracking/world space.
		OverlayRel_DeviceSpace,

		//Overlay follows the reference device's position.
		//Offset is interpreted in the reference device's local axes.
		OverlayRel_DeviceSpaceForward,

		//Overlay follows refrence device's posistion and rotation
		//OverlayRotationFlags are ignored
		OverlayRel_Attached
	};

	enum OverlayRotationFlags : uint32_t
	{
		RotFlag_None = 0,

		RotFlag_UseYaw = 1 << 0,
		RotFlag_UsePitch = 1 << 1,
		RotFlag_UseRoll = 1 << 2,
		RotFlag_UseAll = RotFlag_UseYaw | RotFlag_UsePitch | RotFlag_UseRoll
	};

	struct OverlayRotation
	{
		OverlayRotationFlags flags = RotFlag_None; //Used to define what axis is inherited from target

		float pitchOffset = 0.0f;
		float yawOffset = 0.0f;
		float rollOffset = 0.0f;
	};


	VR() {};
	VR(Game *game);
	~VR();

	//Post initialization setup stage
	void CreateHashMaps();
	int SetActionManifest(const char *fileName);
	void InstallApplicationManifest(const char *fileName);
	void PreUpdate();
	void PostUpdate();
	void FirstFrameUpdate();
	void CreateVRTextures();
	void SubmitVRTextures();

	//Attach mode ignores RotFlags and inherits rotation from reference.
	void RepositionOverlay(vr::VROverlayHandle_t overlay, vr::TrackedDeviceIndex_t referenceDevice, OverlayRelitive con, Vector offset, OverlayRotation rot = {});
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
	int Load3DMenu();
	std::string GetMapFromSave(const char* fileName);
	std::string GetNewestPortal2SavePath(const std::string& baseDir);
	bool ShouldCapture(CaptureConditions con);
	void BuildCaptureMap();
	void CreateRT(SharedTextureHolder* target, const char* name, int w, int h, RenderTargetSizeMode_t sizeMode, ImageFormat format, MaterialRenderTargetDepth_t depth = MATERIAL_RT_DEPTH_SEPARATE, UINT textureFlags = TEXTUREFLAGS_NOMIP);
	const char* EVRInputErrorToString(vr::EVRInputError error);
	void PushTexture(SharedTextureHolder* holder, int isMSAA);
	std::pair<int, SharedTextureHolder*> PopNextTexture();

	//Captures children and descendances of parent panel
	// - VPANEL is used because the capture happens inside the draw function so speed is needed
	// - Return in Lamba function is used to Decide to capture or not
	// - When excluding panel by panel name if you set a texture with it the excluded panel will render to the new target instead
	void RegisterPanelCaptureRoot(VPANEL panel, ITexture* dest, std::function<bool()> func, std::vector<std::pair<const char*, ITexture*>> ExcludeList = {});

	//Override layout loaded by engine 
	// - Default callback returns true to say yes I will override the engine
	// - Can define your own callback to set conditions
	void OverridePanelLayout(std::string TargetLayout, OverrideLayout NewLayout); 

	//Listens to command stream and calls defined function on match. 
	// - Return true to intercept command from being sent out
	// - Can modify message being sent
	void RegisterPanelCommandListener(std::initializer_list<std::string> Commands, std::function<bool(const char* cmd, Panel* panel, KeyValues* message)> func); 

	//Used to modify panels that have settings 
	// - Return true tells program to not apply settings, useful if you will handle the call
	// - Can store the KeyValues in the defined SettingRuntimeData that comes with the panel
	void ModifyPanelSettings(std::string PanelName, std::function<bool(Panel* panel, KeyValues* inResourceData, std::unordered_map<std::string, std::variant<bool, float, int>>& SettingRuntimeData)> func);
};

class VRTextureResolveQueue
{
public:
	std::vector<SharedTextureHolder*> m_textures;

	void RegisterTexture(SharedTextureHolder* tex) {
		if (!tex) return;

		auto it = std::find(m_textures.begin(), m_textures.end(), tex);
		if (it == m_textures.end())
			m_textures.push_back(tex);
	}
};