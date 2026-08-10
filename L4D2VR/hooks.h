#pragma once
#include <iostream>
#include "MinHook.h"
#include "bitbuf.h"


class Game;
class VR;
class ITexture;
class CViewSetup;
class CUserCmd;
class QAngle;
class Vector;
class edict_t;
class ModelRenderInfo_t;
struct trace_tx;
class IMatRenderContext;
struct vrect_t;
struct Ray_t;
struct VMatrix;
struct Rect_t;
class bf_write;
class bf_read;


template <typename T>
struct Hook {
	T fOriginal;
	LPVOID pTarget;
	bool isEnabled;
	std::string* hookName;

	int createHook(std::string* hookName, LPVOID targetFunc, LPVOID detourFunc)
	{
		MH_STATUS status = MH_CreateHook(targetFunc, detourFunc, reinterpret_cast<LPVOID*>(&fOriginal));
		if ( status != MH_OK)
		{
			Game::errorMsg(("Failed to create hook: " + *hookName + " with status: " + MH_StatusToString(status)).c_str());
			return 1;
		}
		pTarget = targetFunc;
		this->hookName = hookName;

		return 0;
	}

	int enableHook()
	{
		if (!pTarget)
			throw std::invalid_argument("pTarget is empty, did you miss a call to createHook?");

		MH_STATUS status = MH_EnableHook(pTarget);
		if (status != MH_OK)
		{
			Game::errorMsg(("Failed to enable hook: " + *this->hookName + ", status: " + MH_StatusToString(status)).c_str());
			return 1;
		}
		isEnabled = true;

		return 0;
	}

	int disableHook()
	{
		MH_STATUS status = MH_DisableHook(pTarget);
		if ( status != MH_OK)
		{
			Game::errorMsg(("Failed to disable hook: " + *this->hookName + ", status: " + MH_StatusToString(status)).c_str());
			return 1;
		}
		isEnabled = false;

		return 0;
	}
};


using tPlayerPortalled = void(__thiscall*)(void* thisptr, void* a2, __int64 a3);

//Movement
using tCreateMove = bool(__thiscall*)(void* thisptr, float flInputSampleTime, CUserCmd* cmd);
using tProcessUsercmds = float(__thiscall*)(void* thisptr, edict_t* player, void* buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused);
using tReadUsercmd = int(__cdecl*)(void* buf, CUserCmd* move, CUserCmd* from);
using tWriteUsercmd = int(__cdecl*)(void* buf, CUserCmd* to, CUserCmd* from);

//Weapon
using tWeapon_ShootPosition = Vector* (__thiscall*)(void* thisptr, Vector* shootPos);
using tCWeaponPortalgun_FirePortal = void* (__thiscall*)(void* thisptr, bool bPortal2, Vector* pVector);
using tTraceFirePortal = bool(__thiscall*)(void* thisptr, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr);

//Rendering
using tRenderView = void(__thiscall*)(void* thisptr, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw);
using tComputeShadowDepthTextures = void(__thiscall*)(void* thisptr, const CViewSetup& pView);
using tCalcViewModelView = void(__thiscall*)(void* thisptr, const Vector& eyePosition, const QAngle& eyeAngles);
using tUnlockAllShadowDepthTextures = void(__thiscall*)(void* thisptr);
using tAdjustEngineViewport = int(__cdecl*)(int& x, int& y, int& width, int& height);
using tGetViewport = void(__thiscall*)(void* thisptr, int& x, int& y, int& width, int& height);
using tDrawModelExecute = void(__thiscall*)(void* thisptr, void* state, const ModelRenderInfo_t& info, void* pCustomBoneToWorld);
using tSetDrawOnlyForSplitScreenUser = void(__thiscall*)(void* thisptr, int nSlot);
using tFormatViewModelAttachment = void(__cdecl*)(void* param_1, Vector& vOrgin, bool bInverse);
using tCWLC_Flush = void(__thiscall*)(void* thisptr);

//In game UI
using tPrepareCredits = void(__thiscall*)(void* thisptr, const char* pKeyName);
using tCHudCrosshair_ShouldDraw = bool(__thiscall*)(void* thisptr);
using tLoadControlSettings = void(__thiscall*)(void* thisptr, const char* dialogResourceName, const char* pathID, KeyValues* pPreloadedKeyValues, KeyValues* pConditions);
using tPostActionSignal = void(__thiscall*)(void* thisptr, KeyValues* message);
using tApplySettings = void(__thiscall*)(void* thisptr, KeyValues* inResourceData);
using tUpdateProgressBar = float(__thiscall*)(void* thisptr);
using tPaintTraverse = void(__thiscall*)(void* thisptr, VPANEL vguiPanel, bool forceRepaint, bool allowForce);

//Grabbles
using tComputeError = double(__thiscall*)(void* thisptr);
using tUpdateObject = bool(__thiscall*)(void* thisptr, void* pPlayer, float flError, bool bIsTeleport);
using tRotateObject = void(__thiscall*)(void* thisptr, void* pPlayer, float fRotAboutUp, float fRotAboutRight, bool bUseWorldUpInsteadOfPlayerUp);
using tEyeAngles = QAngle& (__thiscall*)(void* thisptr);

//For Portal gun VFX
using tGetDefaultFOV =  int(__cdecl*)(void*& thisptr);
using tGetFOV = double(__cdecl*)(void*& thisptr);
using tGetViewModelFOV = double(__cdecl*)(void*& thisptr);

//Direct calls
using tPrecacheParticleSystem = int(__cdecl*)(const char* pParticleSystemName);
using tPushAllowBoneAccess = void(__cdecl*)(bool bAllowForNormalModels, bool bAllowForViewModels, char const* tagPush);
using tPopBoneAccess = void(__cdecl*)(char const* tagPop);
using tUTIL_Portal_FirstAlongRay = void* (__cdecl*)(const Ray_t& ray, float& fMustBeCloserThan);
using tUTIL_IntersectRayWithPortal = float(__cdecl*)(const Ray_t& ray, const void* pPortal);
using tUTIL_Portal_AngleTransform = void(__cdecl*)(const VMatrix& matThisToLinked, const QAngle& qSource, QAngle& qTransformed);
using tEntindex = int(__thiscall*)(void* thisptr);
using tGetOwner = void* (__thiscall*)(void* thisptr);

//Map related
using tLevelInit = bool(__thiscall*)(void* thisptr, const char* pMapName, char const* pMapEntities, char const* pOldLevel, char const* pLandmarkName, bool loadGame, bool background); 


class Hooks
{
public:
	static inline Game *m_Game;
	static inline VR *m_VR;


	static inline Hook<tPlayerPortalled> hkPlayerPortalled;

	//Movement
	static inline Hook<tCreateMove> hkCreateMove;
	static inline Hook<tProcessUsercmds> hkProcessUsercmds;
	static inline Hook<tReadUsercmd> hkReadUsercmd;
	static inline Hook<tWriteUsercmd> hkWriteUsercmd;

	//Weapon
	static inline Hook<tWeapon_ShootPosition> hkWeapon_ShootPosition;
	static inline Hook<tTraceFirePortal> hkTraceFirePortal;
	static inline Hook<tCWeaponPortalgun_FirePortal> hkCWeaponPortalgun_FirePortal;

	//Rendering
	static inline Hook<tRenderView> hkRenderView;
	static inline Hook<tCalcViewModelView> hkCalcViewModelView;
	static inline Hook<tAdjustEngineViewport> hkAdjustEngineViewport;
	static inline Hook<tGetViewport> hkGetViewport;
	static inline Hook<tDrawModelExecute> hkDrawModelExecute;
	static inline Hook<tSetDrawOnlyForSplitScreenUser> hkSetDrawOnlyForSplitScreenUser;
	static inline Hook<tComputeShadowDepthTextures> hkComputeShadowDepthTextures;
	static inline Hook<tUnlockAllShadowDepthTextures> hkUnlockAllShadowDepthTextures;
	static inline Hook<tFormatViewModelAttachment> hkFormatViewModelAttachment;
	static inline Hook<tCWLC_Flush> hkCWLC_Flush;

	//In game UI
	static inline Hook<tPaintTraverse> hkPaintTraverse;
	static inline Hook<tPrepareCredits> hkPrepareCredits;
	static inline Hook<tPostActionSignal> hkPostActionSignal;
	static inline Hook<tLoadControlSettings> hkLoadControlSettings;
	static inline Hook<tApplySettings> hkApplySettings;
	static inline Hook<tUpdateProgressBar> hkUpdateProgressBar;

	//Grabbles
	static inline Hook<tComputeError> hkComputeError;
	static inline Hook<tUpdateObject> hkUpdateObject;
	static inline Hook<tRotateObject> hkRotateObject;
	static inline Hook<tEyeAngles> hkEyeAngles;
	
	//For Portal gun VFX
	static inline Hook<tGetDefaultFOV> hkGetDefaultFOV;
	static inline Hook<tGetFOV> hkGetFOV;
	static inline Hook<tGetViewModelFOV> hkGetViewModelFOV;

	//Map related
	static inline Hook<tLevelInit> hkLevelInit;


	//Direct calls
	static inline tPrecacheParticleSystem PrecacheParticleSystem;
	static inline tPushAllowBoneAccess PushAllowBoneAccess;
	static inline tPopBoneAccess PopBoneAccess;
	static inline tUTIL_Portal_FirstAlongRay UTIL_Portal_FirstAlongRay;
	static inline tUTIL_IntersectRayWithPortal UTIL_IntersectRayWithPortal;
	static inline tUTIL_Portal_AngleTransform UTIL_Portal_AngleTransform;
	static inline tEntindex EntityIndex;
	static inline tGetOwner GetOwner;


	static inline bool m_FirstFrame = true;

	Hooks() {};
	Hooks(Game *game);

	~Hooks();


	//Helpers
	template <typename T>
	void BuildHook(Hook<T>& hook, Offset& offset, LPVOID detourFunc, bool enabled = true) {
		int res = hook.createHook(&offset.hookName, reinterpret_cast<LPVOID>(offset.address), detourFunc);

		if (res && hook.hookName) m_Game->logMsg(LOGTYPE_WARNING, "Failed to create hook: %s", hook.hookName->c_str());
		if (enabled && !res) hook.enableHook();
	}

	template <typename T>
	void BuildDirectCall(T& Direct, Offset& offset)
	{
		Direct = reinterpret_cast<T>(offset.address);
	}

	static inline bool IsPanelExcluded(VPANEL panel, const std::vector<std::pair<const char*, ITexture*>>* excludeList, ITexture*& Override)
	{
		const char* name = m_Game->m_VguiIPanel->GetName(panel);
		if (!name) return false;

		for (const auto& [excludedName, tex] : *excludeList)
		{
			if (!std::strcmp(name, excludedName))
			{
				if (!tex)
					return true;

				Override = tex;
				return false;
			}
		}

		return false;
	}

	static void __fastcall dPlayerPortalled(void* ecx, void* edx, void* a2, __int64 a3);

	//Weapon
	static bool __fastcall dTraceFirePortal(void* ecx, void* edx, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr);
	static void* __fastcall dCWeaponPortalgun_FirePortal(void* ecx, void* edx, bool bPortal2, Vector* pVector = 0);
	static Vector* __fastcall dWeapon_ShootPosition(void* ecx, void* edx, Vector* shootPos);

	//Movement
	static bool __fastcall dCreateMove(void* ecx, void* edx, float flInputSampleTime, CUserCmd* cmd);
	static float __fastcall dProcessUsercmds(void* ecx, void* edx, edict_t* player, void* buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused);
	static int dReadUsercmd(bf_read* buf, CUserCmd* move, CUserCmd* from);
	static int dWriteUsercmd(bf_write* buf, CUserCmd* to, CUserCmd* from);

	//Rendering
	static void __fastcall dRenderView(void *ecx, void *edx, CViewSetup &setup, CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw);
	static void __fastcall dDevRenderView(void* ecx, void* edx, CViewSetup& setup, CViewSetup& hudViewSetup, int nClearFlags, int whatToDraw);
	static void __fastcall dCalcViewModelView(void* ecx, void* edx, const Vector& eyePosition, const QAngle& eyeAngles);
	static void dAdjustEngineViewport(int& x, int& y, int& width, int& height);
	static void __fastcall dGetViewport(void* ecx, void* edx, int& x, int& y, int& width, int& height);
	static void __fastcall dDrawModelExecute(void* ecx, void* edx, void* state, const ModelRenderInfo_t& info, void* pCustomBoneToWorld);
	static void __fastcall dSetDrawOnlyForSplitScreenUser(void* ecx, void* edx, int nSlot);
	static void __fastcall dComputeShadowDepthTextures(void* ecx, void* edx, const CViewSetup& pView);
	static void __fastcall dUnlockAllShadowDepthTextures(void* ecx, void* edx);
	static void dFormatViewModelAttachment(void* param_1, Vector& vOrigin, bool bInverse);
	static void dCWLC_Flush(void* ecx, void* edx);

	//In game UI
	static void __fastcall dPaintTraverse(void* ecx, void* edx, VPANEL vguiPanel, bool forceRepaint, bool allowForce);
	static void __fastcall dPrepareCredits(void* ecx, void* edx, const char* pKeyName);
	static void __fastcall dPostActionSignal(void* ecx, void* edx, KeyValues* message);
	static void __fastcall dLoadControlSettings(void* ecx, void* edx, const char* dialogResourceName, const char* pathID, KeyValues* pPreloadedKeyValues, KeyValues* pConditions);
	static void __fastcall dApplySettings(void* ecx, void* edx, KeyValues* inResourceData);
	static float __fastcall dUpdateProgressBar(void* ecx, void* edx);

	//Grabbles
	static double __fastcall dComputeError(void* ecx, void* edx);
	static bool __fastcall dUpdateObject(void* ecx, void* edx, void* pPlayer, float flError, bool bIsTeleport = false);
	static void __fastcall dRotateObject(void* ecx, void* edx, void* pPlayer, float fRotAboutUp, float fRotAboutRight, bool bUseWorldUpInsteadOfPlayerUp);
	static QAngle& __fastcall dEyeAngles(void* ecx, void* edx);
	
	//For Portal gun VFX
	static int __fastcall dGetDefaultFOV(void* ecx, void* edx);
	static double __fastcall dGetFOV(void* ecx, void* edx);
	static double __fastcall dGetViewModelFOV(void* ecx, void* edx);

	//Map related
	static bool __fastcall dLevelInit(void* ecx, void* edx, const char* pMapName, char const* pMapEntities, char const* pOldLevel, char const* pLandmarkName, bool loadGame, bool background);
};