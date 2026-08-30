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
#include "json.hpp"
#include "sdk.h"

#define SAFE_RELEASE(x) \
    if (x) {            \
        x->Release();   \
        x = nullptr;    \
    }

class Game;
class ITexture;

#pragma region DataTypes
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

	std::string m_Name;

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

	void Release()
	{
		m_SurfaceImage = nullptr;
		m_MSAASurfaceImage = nullptr;

		SAFE_RELEASE(m_Surface);
		SAFE_RELEASE(m_MSAASurface);

		m_Texture = nullptr;
		m_MSAATexture = nullptr;

		SAFE_RELEASE(m_ITex);
		SAFE_RELEASE(m_MSAAITex);

		m_VulkanData = {};
		m_VRTexture = {};
	};
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
	std::string m_Name;
	vr::VROverlayInputMethod m_InputMethod = vr::VROverlayInputMethod_None;
	vr::VROverlayInputMethod m_SaveStateMethod = vr::VROverlayInputMethod_None;
	float m_WidthInMeters = 0;

	Overlay() {}
	~Overlay() 
	{
		if (m_Handle) vr::VROverlay()->DestroyOverlay(m_Handle);
	}

	void Initialize(std::string name)
	{
		vr::IVROverlay* overlay = vr::VROverlay();
		std::string key = name + "Key";

		overlay->CreateOverlay(key.c_str(), name.c_str(), &m_Handle);
		overlay->GetOverlayWidthInMeters(m_Handle, &m_WidthInMeters);
		m_Visible = vr::VROverlay()->IsOverlayVisible(m_Handle);
	}

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

	void SetOverlayWidthInMeters(float fWidthInMeters)
	{
		if (fWidthInMeters != m_WidthInMeters) 
		{
			vr::VROverlay()->SetOverlayWidthInMeters(m_Handle, fWidthInMeters);
			m_WidthInMeters = fWidthInMeters;
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
			Game::logMsg(LOGTYPE_WARNING, "%s not found in PanelSettings", key.c_str());
			return T{};
		}

		if (auto val = std::get_if<T>(&it->second))
			return *val;

		Game::logMsg(LOGTYPE_WARNING, "%s type mismatch in PanelSettings", key.c_str());
		return T{};
	}
};

enum OverlayRotationFlags : uint32_t
{
	RotFlag_None = 0,

	RotFlag_UseYaw = 1 << 0,
	RotFlag_UsePitch = 1 << 1,
	RotFlag_UseRoll = 1 << 2,
	RotFlag_UseAll = RotFlag_UseYaw | RotFlag_UsePitch | RotFlag_UseRoll,

	RotFlag_FaceDevice = 1 << 3
};

struct OverlayRotation
{
	OverlayRotationFlags m_flags = RotFlag_None; //Used to define what axis is inherited from target

	float m_pitchOffset = 0.0f;
	float m_yawOffset = 0.0f;
	float m_rollOffset = 0.0f;

	OverlayRotation() {}

	OverlayRotation(OverlayRotationFlags flag)
	{
		m_flags = flag;
	}

	OverlayRotation(OverlayRotationFlags flag, const Vector& vector)
	{
		m_flags = flag;
		m_pitchOffset = vector.x;
		m_yawOffset = vector.y;
		m_rollOffset = vector.z;
	}

	OverlayRotation(OverlayRotationFlags flag, const QAngle& vector)
	{
		m_flags = flag;
		m_pitchOffset = vector.x;
		m_yawOffset = vector.y;
		m_rollOffset = vector.z;
	}
};

enum OverlayRel
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

	//Overlay follows reference device's position and rotation
	//OverlayRotationFlags are ignored
	OverlayRel_Attached
};

enum CaptureConditions
{
	//CaptureConditions are used as both a input to say what mode you want as well as the return result.

	Capture_None = 0,
	Capture_Any,
	Capture_2D,
	Capture_MenuUI,
	Capture_HudUI
};

enum VRBindingType
{
	VRBindingType_None,

	//Processes when a menu is not active.
	//Supports digital button press/release handling, commands, callbacks, and optional hold/toggle behavior.
	VRBindingType_Input,

	//Processes when a menu is open.
	//Supports digital button press/release handling, commands, callbacks, and optional hold/toggle behavior.
	VRBindingType_Menu,

	//Processes when a menu is not active.
	//Intended for analog actions (joysticks/trackpads).
	//Calls the callback every frame and leaves all input handling to the callback. 
	//Press/release commands and hold behavior are disabled.
	VRBindingType_Analog
};

enum VRBindingMode {
	VRBindingMode_Button,  // Press once
	VRBindingMode_Toggle,  // Press toggles on/off
	VRBindingMode_Hold,    // Press once, release once
	VRBindingMode_Repeat   // Press repeatedly while held
};

enum AimMode
{
	AimMode_None,
	AimMode_Overlay,
	AimMode_AssistBeam
};

struct VRBindings
{
	vr::VRActionHandle_t m_Handle = vr::k_ulInvalidActionHandle;
	std::string m_Name;

	VRBindingType m_BindingType = VRBindingType_None;
	VRBindingMode m_BindingMode = VRBindingMode_Button;

	const char* m_PressCommand = nullptr;
	const char* m_ReleaseCommand = nullptr;

	bool m_LastButtonState = false;
	bool m_ToggleState = false;

	std::function<void(vr::VRActionHandle_t handle)> m_Func;

	VRBindings(std::string name, VRBindingType BindingType, const char* pressCmd, const char* releaseCmd, std::function<void(vr::VRActionHandle_t handle)> func, VRBindingMode mode)
	{
		m_Name = name;
		m_BindingType = BindingType;
		m_PressCommand = pressCmd;
		m_ReleaseCommand = releaseCmd;
		m_Func = func;
		m_BindingMode = mode;
	}
};

struct StringPair {
	std::string m_Str1;
	std::string m_Str2;

	StringPair() {}

	StringPair(std::string str1)
	{
		m_Str1 = str1;
	}

	StringPair(std::string str1, std::string str2) 
	{
		m_Str1 = str1;
		m_Str2 = str2;
	}
};

struct ConfigSettings 
{
	float m_TurnSpeed = 0.15f;
	bool m_SnapTurning = false;
	float m_SnapTurnAngle = 45.0f;
	bool m_LeftHanded = false;
	AimMode m_AimMode = AimMode_AssistBeam;
	bool m_6DOF = true;
	float m_CrosshairDistance = 1.0f;

	float m_VRScale = 43.2f;
	float m_IpdScale = 1.0f;
	uint32_t m_AntiAliasing = 0;
	bool m_3DBackground = false;
	bool m_RenderWindow = false;
	int m_ExperimentalOptimizations = 0;
	bool m_UsePCCM = false;

	float m_PortallingDetectionDistanceThreshold = 35.0f;
	bool m_SmoothPortalRotation = false;
	float m_CameraUprightRecoverySpeed = 0.2f;
	
	bool m_UseRoomScale = true;
};
inline void from_json(const nlohmann::json& j, ConfigSettings& out)
{
	auto parse = [&](const char* name, auto& value)
	{
		try
		{
			if (j.contains(name)) j.at(name).get_to(value);
		}
		catch (const std::exception& e)
		{
			g_Game->logMsg(LOGTYPE_WARNING, "Failed to parse ConfigSettings.%s: %s. Using default value.", name, e.what());
		}
	};

	parse("TurnSpeed", out.m_TurnSpeed);
	parse("SnapTurning", out.m_SnapTurning);
	parse("SnapTurnAngle", out.m_SnapTurnAngle);
	parse("LeftHanded", out.m_LeftHanded);
	parse("AimMode", out.m_AimMode);
	parse("6DOF", out.m_6DOF);
	parse("CrosshairDistance", out.m_CrosshairDistance);

	parse("VRScale", out.m_VRScale);
	parse("IPDScale", out.m_IpdScale);
	parse("AntiAliasing", out.m_AntiAliasing);
	parse("RenderWindow", out.m_RenderWindow);
	parse("Enable3DBackground", out.m_3DBackground);
	parse("ExperimentalOptimizations", out.m_ExperimentalOptimizations);
	parse("UsePCCMs", out.m_UsePCCM);

	parse("PortallingDetectionDistanceThreshold", out.m_PortallingDetectionDistanceThreshold);

	parse("CameraUprightRecoverySpeed", out.m_CameraUprightRecoverySpeed);

	parse("SmoothPortalRotation", out.m_SmoothPortalRotation);
	parse("UseRoomScale", out.m_UseRoomScale);
}

struct ViewModelConfigSettings {
	Vector m_PositionOffset = { 0, 0, 0 };
	QAngle m_AngleOffset = { 0, 0, 0 };
	bool m_CanUseAssistBeam = false;
	std::string m_AssistBeamAttachment;
	bool m_CanUseLeftHand = true;

	bool m_IsValid = false;
	bool m_Failed = false; //Mark failure to even get the player or weapon

	C_BaseCombatWeapon* m_ActiveWeapon = nullptr;

	bool UsableEntry()
	{
		return (m_IsValid && !m_Failed);
	}

	void ResetPreserveWeapon() 
	{
		C_BaseCombatWeapon* temp = m_ActiveWeapon;
		
		*this = ViewModelConfigSettings{};
		m_ActiveWeapon = temp;
	}
};
inline void from_json(const nlohmann::json& j, ViewModelConfigSettings& out)
{
	auto parse = [&](const char* name, auto& value)
	{
		try
		{
			if (j.contains(name)) j.at(name).get_to(value);
		}
		catch (const std::exception& e)
		{
			g_Game->logMsg(LOGTYPE_WARNING, "Failed to parse ViewModelConfigSettings.%s: %s. Using default value.", name, e.what());
		}
	};

	parse("PositionOffset", out.m_PositionOffset);
	parse("AngleOffset", out.m_AngleOffset);
	parse("CanUseAssistBeam", out.m_CanUseAssistBeam);
	parse("AssistBeamAttachment", out.m_AssistBeamAttachment);
	parse("CanUseLeftHand", out.m_CanUseLeftHand);
}
#pragma endregion

void CreateRT(SharedTextureHolder* target, const char* name, int w, int h, RenderTargetSizeMode_t sizeMode, ImageFormat format, MaterialRenderTargetDepth_t depth = MATERIAL_RT_DEPTH_SEPARATE, UINT textureFlags = TEXTUREFLAGS_NOMIP);

//Push/Pop texture is used to account for any delay between the game calling to create texture and dxvk creating it
void PushTexture(SharedTextureHolder* holder, int isMSAA);
std::pair<int, SharedTextureHolder*> PopNextTexture();

class VR
{
public:
	friend class PCCM;

#pragma region Variables
	Game* m_Game = nullptr;
	vr::IVRSystem *m_System = nullptr;
	vr::IVRInput *m_Input = nullptr;
	vr::IVROverlay *m_Overlay = nullptr;
	vr::IVRCompositor* m_Compositor = nullptr;

	Overlay m_MainOverlay;
	Overlay m_HudOverlay;

	float m_WScaleDownRatio, m_HScaleDownRatio, m_WScaleUpRatio, m_HScaleUpRatio;
	uint32_t m_RenderWidth = 0, m_RenderHeight = 0;
	float m_Aspect = 0;
	float m_Fov = 0;
	float m_Ipd = 0;
	float m_EyeZ = 0;


	Vector m_EyeToHeadTransformPosLeft = { 0,0,0 };
	Vector m_EyeToHeadTransformPosRight = { 0,0,0 };

	Vector m_LastHmdPos;

	Vector m_HmdForward;
	Vector m_HmdRight;
	Vector m_HmdUp;

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

	Vector m_AimPos = { 0, 0, 0 }; //Traced from controller

	Vector m_Center = { 0,0,0 };
	Vector m_SetupOrigin = { 0,0,0 };

	Vector m_LeftControllerPosAbs;											
	QAngle m_LeftControllerAngAbs;
	Vector m_RightControllerPosRel;											
	QAngle m_RightControllerAngAbs;

	Vector m_ViewmodelPosOffset;
	QAngle m_ViewmodelAngOffset;

	Vector m_IntendedPositionOffset = { 0,0,0 };

	TrackedDevicePoseData m_HmdPose;
	TrackedDevicePoseData m_LeftControllerPose;
	TrackedDevicePoseData m_RightControllerPose;

	QAngle m_PortalRotationOffset = { 0, 0, 0 };
	QAngle m_RotationOffset = { 0, 0, 0 };

	std::chrono::steady_clock::time_point m_PrevFrameTime;

#pragma region Textures
	vr::VRTextureBounds_t m_TextureBounds[2];
	vr::TrackedDevicePose_t m_Poses[vr::k_unMaxTrackedDeviceCount] = {};
	vr::EVRCompositorError m_LastLeftEyeError = vr::VRCompositorError_None, m_LastRightEyeError = vr::VRCompositorError_None;

	SharedTextureHolder m_LeftEye;
	SharedTextureHolder m_RightEye;
	SharedTextureHolder m_BackBuffer;
	SharedTextureHolder m_BlankTexture;
	SharedTextureHolder m_MenuTexture;
	SharedTextureHolder m_HudTexture;
#pragma endregion

#pragma region StateToggles
	bool m_IsVREnabled = false;
	bool m_IsInitialized = false;
	bool m_CreatedVRTextures = false;
	bool m_PressedTurn = false;
	bool m_IsRightEye = false;
	bool m_ParticleCached = false;
	bool m_BuiltCaptureMap = false;
	bool m_IsLevelBackground = false;
	bool m_IsCredits = false;
	bool m_OverrideControllerUI = false;
	bool m_LevelExitFix = false; //Disconnects from level before exiting level to fix soft lock
	bool m_3DMenuLoading = false;
	bool m_ApplyPortalRotationOffset = false;
	bool m_OverrideEyeAngles = false;
	bool m_CurrentWeaponSettingsStale = false;
#pragma endregion

#pragma region Maps
	std::unordered_map<std::string, std::string> m_BackgroundMapping{}; //Map map names to background names
	std::unordered_map<VPANEL, PanelCaptureInfo> m_PanelCaptureMap{}; //Captures the children of the parent panel
	std::unordered_map<std::string, OverrideLayout> m_PanelLayoutOverride{}; //Override layout for loaded panels
	std::unordered_map<std::string, std::function<bool(const char* cmd, Panel* panel, KeyValues* message)>> m_PanelCommands{}; //Listen or override commands from panels
	std::unordered_map<std::string, PanelSettings> m_PanelSettings{}; //Used to modify panel settings
	std::unordered_map<std::string, std::function<void(Panel* panel, float Percentage)>> m_SlideRead;
#pragma endregion

#pragma region ActionsAndBinds
	vr::VRActionSetHandle_t m_ActionSet = {};
	vr::VRActiveActionSet_t m_ActiveActionSet = {};
	std::vector<VRBindings> m_Bindings;
	vr::VRActionHandle_t m_ActionWalk = vr::k_ulInvalidActionHandle;
#pragma endregion

#pragma region Configs
	ConfigSettings m_Config;
	std::unordered_map<std::string, ViewModelConfigSettings> m_VMConfig; //Viewmodel config settings
#pragma endregion

	std::pair<std::string, ViewModelConfigSettings> m_CurrentWeapon;
	CNewParticleEffect* m_AssistBeam;
#pragma endregion

#pragma region Helpers
	template <typename T>
	void WriteConfigEntry(const std::string& key, const T& value)
	{
		const char* filePath = "VR\\config.json";

		std::ifstream inFile(filePath);
		if (!inFile.is_open())
		{
			g_Game->logMsg(LOGTYPE_WARNING, "Could not open config file for writing: %s", filePath);
			return;
		}

		std::vector<std::string> lines;
		std::string line;

		std::string valueStr;

		if constexpr (std::is_same_v<T, bool>)
		{
			valueStr = value ? "true" : "false";
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			nlohmann::json jsonValue = value;
			valueStr = jsonValue.dump();
		}
		else if constexpr (std::is_arithmetic_v<T>)
		{
			nlohmann::json jsonValue = value;
			valueStr = jsonValue.dump();
		}
		else
		{
			g_Game->logMsg(LOGTYPE_WARNING, "Unsupported config type for key: %s",key.c_str());
			return;
		}

		bool found = false;

		while (std::getline(inFile, line))
		{
			const std::string quotedKey = "\"" + key + "\"";
			const size_t keyPos = line.find(quotedKey);

			if (keyPos != std::string::npos)
			{
				const size_t colonPos = line.find(':', keyPos + quotedKey.length());
				if (colonPos != std::string::npos)
				{
					size_t valueStart = colonPos + 1;

					while (valueStart < line.length() && std::isspace(static_cast<unsigned char>(line[valueStart])))
					{
						++valueStart;
					}

					size_t commentPos = line.find("//", valueStart);
					size_t valueEnd;

					if (commentPos != std::string::npos)
					{
						valueEnd = commentPos;
						while (valueEnd > valueStart && std::isspace(static_cast<unsigned char>(line[valueEnd - 1])))
						{
							--valueEnd;
						}
					}
					else
					{
						valueEnd = line.length();

						while (valueEnd > valueStart && std::isspace(static_cast<unsigned char>(line[valueEnd - 1])))
						{
							--valueEnd;
						}
					}

					bool hasComma = false;
					if (valueEnd > valueStart && line[valueEnd - 1] == ',')
					{
						hasComma = true;
						--valueEnd;

						while (valueEnd > valueStart && std::isspace(static_cast<unsigned char>(line[valueEnd - 1])))
						{
							--valueEnd;
						}
					}

					std::string newLine = line.substr(0, valueStart);
					newLine += valueStr;

					if (hasComma) newLine += ",";

					if (commentPos != std::string::npos)
					{
						newLine += " ";
						newLine += line.substr(commentPos);
					}

					line = newLine;
					found = true;
				}
			}

			lines.push_back(line);
		}

		inFile.close();
		if (!found)
		{
			g_Game->logMsg(LOGTYPE_WARNING, "Could not find config entry: %s", key.c_str());
			return;
		}

		std::ofstream outFile(filePath, std::ios::trunc);
		if (!outFile.is_open())
		{
			g_Game->logMsg(LOGTYPE_WARNING, "Could not open config file for writing: %s", filePath);
			return;
		}

		for (const auto& l : lines)
			outFile << l << '\n';

		outFile.close();
	}

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

	template<typename MapType>
	bool LoadStringMap(const char* filePath, const char* key, MapType& map)
	{
		static_assert(std::is_same_v<typename MapType::key_type, std::string>, "LoadStringMap requires std::string keys");
		using ValueType = typename MapType::mapped_type;

		std::ifstream file(filePath);
		if (!file.is_open())
		{
			Game::logMsg(LOGTYPE_WARNING, "Could not open file: %s", filePath);
			return false;
		}

		//Parse file
		nlohmann::json json;
		try
		{
			json = nlohmann::json::parse(file, nullptr, true, true);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			Game::logMsg(LOGTYPE_WARNING, "JSON parse error in %s: %s", filePath, e.what());
			return false;
		}
		catch (const std::exception& e)
		{
			Game::logMsg(LOGTYPE_WARNING, "JSON error in %s: %s", filePath, e.what());
			return false;
		}

		auto section = json.find(key);
		if (section == json.end() || !section->is_object())
		{
			Game::logMsg(LOGTYPE_WARNING, "Could not find key in %s: %s", filePath, key);
			return false;
		}

		map.clear();

		for (const auto& [k, v] : section->items())
		{
			try
			{
				map[k] = v.get<ValueType>();
			}
			catch (const std::exception& e)
			{
				Game::logMsg(LOGTYPE_WARNING, "Failed converting JSON value '%s' in %s: %s", k.c_str(), filePath, e.what());
			}
		}

		return !map.empty();
	}

	const char* EVRInputErrorToString(vr::EVRInputError error)
	{
		switch (error)
		{
			case vr::VRInputError_None: return "None";
			case vr::VRInputError_NameNotFound: return "Name Not Found";
			case vr::VRInputError_WrongType: return "Wrong Type";
			case vr::VRInputError_InvalidHandle: return "Invalid Handle";
			case vr::VRInputError_InvalidParam: return "Invalid Param";
			case vr::VRInputError_NoSteam: return "No Steam running";
			case vr::VRInputError_MaxCapacityReached: return "Max Capacity Reached";
			case vr::VRInputError_IPCError: return "IPC Error";
			case vr::VRInputError_NoActiveActionSet: return "No Active Action Set";
			case vr::VRInputError_InvalidDevice: return "Invalid Device";
			case vr::VRInputError_InvalidSkeleton: return "Invalid Skeleton";
			case vr::VRInputError_InvalidBoneCount: return "Invalid Bone Count";
			case vr::VRInputError_InvalidCompressedData: return "Invalid Compressed Data";
			case vr::VRInputError_NoData: return "No Data";
			case vr::VRInputError_BufferTooSmall: return "Buffer Too Small";
			case vr::VRInputError_MismatchedActionManifest: return "Mismatched Action Manifest";
			case vr::VRInputError_MissingSkeletonData: return "Missing Skeleton Data";
			case vr::VRInputError_InvalidBoneIndex: return "Invalid Bone Index";
			case vr::VRInputError_InvalidPriority: return "Invalid Priority";
			case vr::VRInputError_PermissionDenied: return "Permission Denied";
			case vr::VRInputError_InvalidRenderModel: return "Invalid Render Model";
			default: return "Unknown VRInputError";
		}
	}

	const char* CompositorErrorToString(vr::EVRCompositorError err)
	{
		switch (err)
		{
			case vr::VRCompositorError_None: return "None";
			case vr::VRCompositorError_RequestFailed: return "RequestFailed";
			case vr::VRCompositorError_IncompatibleVersion: return "IncompatibleVersion";
			case vr::VRCompositorError_DoNotHaveFocus: return "DoNotHaveFocus";
			case vr::VRCompositorError_InvalidTexture: return "InvalidTexture";
			case vr::VRCompositorError_IsNotSceneApplication: return "IsNotSceneApplication";
			case vr::VRCompositorError_TextureIsOnWrongDevice: return "TextureIsOnWrongDevice";
			case vr::VRCompositorError_TextureUsesUnsupportedFormat: return "TextureUsesUnsupportedFormat";
			case vr::VRCompositorError_SharedTexturesNotSupported: return "SharedTexturesNotSupported";
			case vr::VRCompositorError_IndexOutOfRange: return "IndexOutOfRange";
			case vr::VRCompositorError_AlreadySubmitted: return "AlreadySubmitted";
			default: return "Unknown";
		}
	}

	void SendButton(int key)
	{
		INPUT input{};
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = key;
		SendInput(1, &input, sizeof(INPUT));
		input.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &input, sizeof(INPUT));
	}

	const char* OverlayRelToString(OverlayRel con)
	{
		switch (con)
		{
			case OverlayRel_WorldSpace: return "WorldSpace";
			case OverlayRel_DeviceSpace: return "DeviceSpace";
			case OverlayRel_DeviceSpaceForward: return "DeviceSpaceForward";
			case OverlayRel_Attached: return "Attached";
			default: return "Unknown";
		}
	}
#pragma endregion

	//====================
	// Functions
	//====================
#pragma region Setup
	VR() {};
	VR(Game *game);
	~VR();

	void CreateHashMaps(); //Post initialization setup stage
	int SetActionManifest(const char *fileName);
	void InstallApplicationManifest(const char *fileName);
	void SetBinding(const char* pchActionName, VRBindingType bindingType, StringPair cmds, VRBindingMode mode = VRBindingMode_Button, std::function<void(vr::VRActionHandle_t handle)> func = [](vr::VRActionHandle_t) {});
#pragma endregion

#pragma region Rendering/Texture
	void PreUpdate();
	void PostUpdate();
	void FirstFrameUpdate();
	void CreateVRTextures();
	void SubmitVRTextures();
	void DeviceReset(); //When D3D reset's this is called
	void BuildCaptureMap();
	int Load3DMenu();
	CaptureConditions ShouldCapture(CaptureConditions con);
	void CreateAssistBeam(Vector vecDestintaion);
#pragma endregion

#pragma region UI
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

	//Attach mode ignores RotFlags and inherits position and rotation from reference.
	void RepositionOverlay(Overlay& overlay, vr::TrackedDeviceIndex_t referenceDevice, OverlayRel con, Vector offset, OverlayRotation rot = {});
#pragma endregion

#pragma region ConfigFileParsers
	void ParseConfigFile(const std::filesystem::path& file);
	void WaitForConfigUpdate();
	std::string GetMapFromSave(const char* fileName);
	std::string GetNewestPortal2SavePath(const std::string& baseDir);
#pragma endregion

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
	Vector Trace(uint32_t* localPlayer);
	Vector TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle);
	void OnMapLoading(const char* mapName, bool isBackground);
	void CheckWeaponType();
};

//Ques up textures that use msaa to be resolved before compositing
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