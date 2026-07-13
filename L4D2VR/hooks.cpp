#include "hooks.h"
#include "game.h"
#include "texture.h"
#include "sdk.h"
#include "sdk_server.h"
#include "vr.h"
#include "offsets.h"
#include <iostream>
#include "util.h"


//#define PrintTraverseNames //Print panel names
//#define PrintCompositerTraverseal //Print VGUI parent traversal during compositor capture lookup.
//#define ResFiles //Print resource file names
//#define PanelCommands //Print panels commands
//#define PrintPanelSettings //Print panel settings if it calls Apply settings

Hooks::Hooks(Game *game)
{
	if (MH_Initialize() != MH_OK)
	{
		Game::errorMsg("Failed to init MinHook");
	}

	m_Game = game;
	m_VR = m_Game->m_VR;

	Offsets* O = m_Game->m_Offsets;

#ifdef OVERRIDEVRMODE
	return;
#endif

	BuildHook(hkPlayerPortalled, O->PlayerPortalled, dPlayerPortalled);
	
	//Movement
	BuildHook(hkProcessUsercmds, O->ProcessUsercmds, dProcessUsercmds);
	BuildHook(hkReadUsercmd, O->ReadUserCmd, dReadUsercmd);
	BuildHook(hkWriteUsercmd, O->WriteUsercmd, dWriteUsercmd);
	BuildHook(hkCreateMove, O->CreateMove, dCreateMove);

	//Weapon
	BuildHook(hkWeapon_ShootPosition, O->Weapon_ShootPosition, dWeapon_ShootPosition);
	BuildHook(hkTraceFirePortal, O->TraceFirePortalServer, dTraceFirePortal);
	BuildHook(hkCWeaponPortalgun_FirePortal, O->CWeaponPortalgun_FirePortal, dCWeaponPortalgun_FirePortal);

	//Rendering
	BuildHook(hkRenderView, O->RenderView, dRenderView);
	BuildHook(hkCalcViewModelView, O->CalcViewModelView, dCalcViewModelView);
	BuildHook(hkSetDrawOnlyForSplitScreenUser, O->SetDrawOnlyForSplitScreenUser, dSetDrawOnlyForSplitScreenUser);
	BuildHook(hkComputeShadowDepthTextures, O->ComputeShadowDepthTextures, dComputeShadowDepthTextures);
	BuildHook(hkUnlockAllShadowDepthTextures, O->UnlockAllShadowDepthTextures, dUnlockAllShadowDepthTextures);
	BuildHook(hkFormatViewModelAttachment, O->FormatViewModelAttachment, dFormatViewModelAttachment);
	
	//In game UI
	BuildHook(hkPostActionSignal, O->PostActionSignal, dPostActionSignal);
	BuildHook(hkLoadControlSettings, O->LoadControlSettings, dLoadControlSettings);
	BuildHook(hkApplySettings, O->ApplySettings, dApplySettings);
	BuildHook(hkUpdateProgressBar, O->UpdateProgressBar, dUpdateProgressBar);
	BuildHook(hkPrepareCredits, O->PrepareCredits, dPrepareCredits);
	BuildHook(hkPaintTraverse, O->PaintTraverse, dPaintTraverse);
	
	// Grabbles
	BuildHook(hkComputeError, O->ComputeError, dComputeError, false);
	BuildHook(hkUpdateObject, O->UpdateObject, dUpdateObject);
	BuildHook(hkRotateObject, O->RotateObject, dRotateObject, false);
	BuildHook(hkEyeAngles, O->EyeAngles, dEyeAngles);

	//Portal Gun VFX
	BuildHook(hkGetDefaultFOV, O->GetDefaultFOV, dGetDefaultFOV);
	BuildHook(hkGetFOV, O->GetFOV, dGetFOV);
	BuildHook(hkGetViewModelFOV, O->GetViewModelFOV, dGetViewModelFOV);

	//Map related
	BuildHook(hkLevelInit, O->LevelInit, dLevelInit);

	//Direct Calls
	BuildDirectCall(UTIL_Portal_FirstAlongRay, O->UTIL_Portal_FirstAlongRay);
	BuildDirectCall(UTIL_IntersectRayWithPortal, O->UTIL_IntersectRayWithPortal);
	BuildDirectCall(UTIL_Portal_AngleTransform, O->UTIL_Portal_AngleTransform);
	BuildDirectCall(CreatePingPointer, O->CreatePingPointer);
	BuildDirectCall(PrecacheParticleSystem, O->PrecacheParticleSystem);
	BuildDirectCall(EntityIndex, O->CBaseEntity_entindex);
	BuildDirectCall(GetOwner, O->GetOwner);
}

Hooks::~Hooks()
{
	if (MH_Uninitialize() != MH_OK)
		Game::errorMsg("Failed to uninitialize MinHook");
}

void __fastcall Hooks::dSetDrawOnlyForSplitScreenUser(void* ecx, void* edx, int nSlot) {
	hkSetDrawOnlyForSplitScreenUser.fOriginal(ecx, -1);
}

//Renders the world
void __fastcall Hooks::dRenderView(void *ecx, void *edx, CViewSetup &setup, CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw)
{
	if (!m_VR->m_CreatedVRTextures)
		m_VR->CreateVRTextures();
	
	//First frame commands
	if (m_FirstFrame)
	{
		m_VR->FirstFrameUpdate();
		m_FirstFrame = false;
	}

	hudViewSetup.width = m_VR->m_RenderWidth;
	hudViewSetup.height = m_VR->m_RenderHeight;
	hudViewSetup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	hudViewSetup.m_nUnscaledHeight = m_VR->m_RenderHeight;
	hudViewSetup.fov = m_VR->m_Fov;
	//hudViewSetup.fovViewmodel = m_VR->m_Fov;
	hudViewSetup.m_flAspectRatio = m_VR->m_Aspect;

	Vector position = setup.origin;

	if (m_VR->m_ApplyPortalRotationOffset) {
		Vector vec = position - m_VR->m_SetupOrigin;
		float distance = sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

		// Rudimentary portalling detection
		if (distance > m_VR->m_PortallingDetectionDistanceThreshold) {
			m_VR->m_RotationOffset.y += m_VR->m_PortalRotationOffset.y;
			
			if (m_VR->m_SmoothRotation) {
				m_VR->m_RotationOffset.x += m_VR->m_PortalRotationOffset.x;
				m_VR->m_RotationOffset.z += m_VR->m_PortalRotationOffset.z;
			}

			m_VR->UpdateHMDAngles();

			m_VR->m_ApplyPortalRotationOffset = false;
		}
	}

	m_VR->m_SetupOrigin = position;

	Vector hmdAngle = m_VR->GetViewAngle();
	QAngle inGameAngle(hmdAngle.x, hmdAngle.y, hmdAngle.z);
	m_Game->m_EngineClient->SetViewAngles(inGameAngle);

	setup.x = 0;
	setup.y = 0;
	setup.width = m_VR->m_RenderWidth;
	setup.height = m_VR->m_RenderHeight;
	setup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	setup.m_nUnscaledHeight = m_VR->m_RenderHeight;
	setup.fov = m_VR->m_Fov;
	setup.fovViewmodel = m_VR->m_Fov;
	setup.m_flAspectRatio = m_VR->m_Aspect;
	setup.zNear = 6;
	setup.zNearViewmodel = 2;
	setup.angles = hmdAngle;

	C_BasePlayer* localPlayer = (C_BasePlayer*)m_Game->GetClientEntity(m_Game->m_EngineClient->GetLocalPlayer());
	IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();

	//Rendering Eyes
	for (size_t I = 0; I < 2; I++) 
	{
		m_VR->m_IsRightEye = I;
		int drawFlags = (!I) ? ((whatToDraw & ~RENDERVIEW_DRAWHUD) | RENDERVIEW_SUPPRESSMONITORRENDERING) : whatToDraw;
		QAngle tempAngle(setup.angles.x, setup.angles.y, setup.angles.z);
		CViewSetup EyeView = setup;
		Vector EyePos = (!I) ? m_VR->GetViewOriginLeft(m_VR->m_SetupOrigin) : m_VR->GetViewOriginRight(m_VR->m_SetupOrigin);
		SharedTextureHolder* TargetTex = (!I) ? &m_VR->m_LeftEye : &m_VR->m_RightEye;
		ITexture* TargetSur = (m_VR->m_AntiAliasing) ? TargetTex->m_MSAAITex : TargetTex->m_ITex;

		EyeView.origin = m_VR->TraceEye((uint32_t*)localPlayer, position, EyePos, tempAngle);
		EyeView.angles.y = tempAngle.y;

		rndrContext->SetRenderTarget(TargetSur);
		hkRenderView.fOriginal(ecx, EyeView, hudViewSetup, nClearFlags, drawFlags);
	}

	m_VR->m_IsRightEye = false;
	rndrContext->SetRenderTarget(NULL);
	rndrContext->Release();
}

//Movement controls
bool __fastcall Hooks::dCreateMove(void *ecx, void *edx, float flInputSampleTime, CUserCmd *cmd)
{
	if (!cmd->command_number)
		return hkCreateMove.fOriginal(ecx, flInputSampleTime, cmd);

	if (m_VR->m_IsVREnabled)
	{
		cmd->viewangles = m_VR->m_HmdAngAbs;

		vr::InputAnalogActionData_t analogActionData;
		if (m_VR->GetAnalogActionData(m_VR->m_ActionWalk, analogActionData)) {
			// Run toward other guy
			cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);

			cmd->forwardmove += analogActionData.y * MAX_LINEAR_SPEED;
			cmd->sidemove += analogActionData.x * MAX_LINEAR_SPEED;

			// We'll only be moving fwd or sideways
			cmd->upmove = 0.0f;

			if (cmd->forwardmove > 0.0f)
			{
				cmd->buttons |= IN_FORWARD;
			}
			else if (cmd->forwardmove < 0.0f)
			{
				cmd->buttons |= IN_BACK;
			}

			if (cmd->sidemove > 0.0f)
			{
				cmd->buttons |= IN_MOVELEFT;
			}
			else if (cmd->sidemove < 0.0f)
			{
				cmd->buttons |= IN_MOVERIGHT;
			}

		}

		if (m_VR->m_RoomscaleActive)
		{
			// How much have we moved since last CreateMove?
			Vector setupOriginToHMD = (m_VR->m_HmdPosRelativeRaw - m_VR->m_HmdPosRelativeRawPrev) * m_VR->m_VRScale; //m_VR->m_HmdPosRelative - m_VR->m_HmdPosRelativePrev;
			m_VR->m_HmdPosRelativeRawPrev = m_VR->m_HmdPosRelativeRaw;

			setupOriginToHMD.z = 0;
			float distance = VectorLength(setupOriginToHMD);
			if (distance > 0.001f)
			{
				float forwardSpeed = DotProduct2D(setupOriginToHMD, m_VR->m_HmdForward);
				float sideSpeed = DotProduct2D(setupOriginToHMD, m_VR->m_HmdRight);
				cmd->forwardmove += distance * forwardSpeed;
				cmd->sidemove += distance * sideSpeed;

				// Let's update the position and the previous too
				/*m_VR->m_HmdPosRelative -= setupOriginToHMD;
				m_VR->m_HmdPosRelativePrev = m_VR->m_HmdPosRelative;*/

				/*m_VR->m_Center += m_VR->m_HmdPosRelativeRaw - m_VR->m_HmdPosRelativeRawPrev;
				m_VR->m_HmdPosRelativeRawPrev = m_VR->m_HmdPosRelativeRaw;*/

				//m_VR->ResetPosition();
			}
		}
	}

	return false;
}

//Positions portal gun on controller
void __fastcall Hooks::dCalcViewModelView(void *ecx, void *edx, const Vector &eyePosition, const QAngle &eyeAngles)
{
	Vector vecNewOrigin = eyePosition;
	QAngle vecNewAngles = eyeAngles;

	if (m_VR->m_IsVREnabled)
	{
		vecNewOrigin = m_VR->GetRecommendedViewmodelAbsPos(eyePosition);
		vecNewAngles = m_VR->GetRecommendedViewmodelAbsAngle();
	}

	return hkCalcViewModelView.fOriginal(ecx, vecNewOrigin, vecNewAngles);
}

float __fastcall Hooks::dProcessUsercmds(void *ecx, void *edx, edict_t *player, void *buf, int numcmds, int totalcmds, int dropped_packets, bool ignore, bool paused)
{
	Server_BaseEntity *pPlayer = (Server_BaseEntity*)player->m_pUnk->GetBaseEntity();

	int index = EntityIndex(pPlayer);
	m_Game->m_CurrentUsercmdID = index;

	return hkProcessUsercmds.fOriginal(ecx, player, buf, numcmds, totalcmds, dropped_packets, ignore, paused);
}

int Hooks::dWriteUsercmd(bf_write *buf, CUserCmd *to, CUserCmd *from)
{
	auto result =  hkWriteUsercmd.fOriginal(buf, to, from);

	// Let's write our stuff into the buffer
	if (m_VR->m_IsVREnabled)
	{
		Vector controllerPos = m_VR->GetRightControllerAbsPos();
		QAngle controllerAngles = m_VR->GetRightControllerAbsAngle();

		buf->WriteChar(-2);
		buf->WriteBitVec3Coord(controllerPos);
		buf->WriteBitAngles(controllerAngles);
	}

	return result;
}

int Hooks::dReadUsercmd(bf_read *buf, CUserCmd* move, CUserCmd* from)
{
	auto result = hkReadUsercmd.fOriginal(buf, move, from);

	int i = m_Game->m_CurrentUsercmdID;
	auto vrPlayer = m_Game->m_PlayersVRInfo[i];

	auto pos = buf->Tell();
	int res = buf->ReadChar();

	// This means we got a VR player on the other side
	if (res == -2)
	{
		vrPlayer.isUsingVR = true;
		buf->ReadBitVec3Coord(vrPlayer.controllerPos);
		buf->ReadBitAngles(vrPlayer.controllerAngle);
	}
	else {
		vrPlayer.isUsingVR = false;
		buf->Seek(pos);
	}

	return result;
}


void Hooks::dAdjustEngineViewport(int &x, int &y, int &width, int &height)
{
	width = m_VR->m_RenderWidth;
	height = m_VR->m_RenderHeight;

	hkAdjustEngineViewport.fOriginal(x, y, width, height);
}

void Hooks::dGetViewport(void *ecx, void *edx, int &x, int &y, int &width, int &height)
{
	hkGetViewport.fOriginal(ecx, x, y, width, height);

	width = m_VR->m_RenderWidth;
	height = m_VR->m_RenderHeight;
}

// We'll keep this for... future reference!
void Hooks::dDrawModelExecute(void *ecx, void *edx, void *state, const ModelRenderInfo_t &info, void *pCustomBoneToWorld)
{
	if (info.pModel)
	{
		std::string modelName = m_Game->m_ModelInfo->GetModelName(info.pModel);
		if (modelName.find("/arms/") != std::string::npos)
		{
			m_Game->m_ArmsMaterial = m_Game->m_MaterialSystem->FindMaterial(modelName.c_str(), "Model textures");
			m_Game->m_ArmsModel = info.pModel;
			m_Game->m_CachedArmsModel = true;
		}
	}

	if (info.pModel && info.pModel == m_Game->m_ArmsModel)
	{
		m_Game->m_ArmsMaterial->SetMaterialVarFlag(MATERIAL_VAR_NO_DRAW, true);
		m_Game->m_ModelRender->ForcedMaterialOverride(m_Game->m_ArmsMaterial);
		hkDrawModelExecute.fOriginal(ecx, state, info, pCustomBoneToWorld);
		m_Game->m_ModelRender->ForcedMaterialOverride(NULL);
		return;
	}

	hkDrawModelExecute.fOriginal(ecx, state, info, pCustomBoneToWorld);
}

Vector* Hooks::dWeapon_ShootPosition(void* ecx, void* edx, Vector* eyePos)
{
	Vector* result = hkWeapon_ShootPosition.fOriginal(ecx, eyePos);

	int localIndex = m_Game->m_EngineClient->GetLocalPlayer();
	int index = EntityIndex(ecx);

	auto vrPlayer = m_Game->m_PlayersVRInfo[index];

	if (m_VR->m_IsVREnabled && localIndex == index) {
		*result = m_VR->GetRightControllerAbsPos();	
	}
	else if (vrPlayer.isUsingVR)
	{
		*result = vrPlayer.controllerPos;
	}

	return result;
}

void* Hooks::dCWeaponPortalgun_FirePortal(void* ecx, void* edx, bool bPortal2, Vector* pVector) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	auto result = hkCWeaponPortalgun_FirePortal.fOriginal(ecx, bPortal2, pVector);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return result;
}

bool __fastcall Hooks::dTraceFirePortal(void* ecx, void* edx, const Vector& vTraceStart, const Vector& vDirection, bool bPortal2, int iPlacedBy, void* tr) //trace_tx& tr, Vector& vFinalPosition //  , Vector& vFinalPosition, QAngle& qFinalAngles, int iPlacedBy, bool bTest /*= false*/
{
	Vector vNewTraceStart = vTraceStart;
	Vector vNewDirection = vDirection;

	if (iPlacedBy == 2) {
		int localIndex = m_Game->m_EngineClient->GetLocalPlayer();

		auto owner = GetOwner(ecx);

		if (owner) {
			int index = EntityIndex(owner);

			auto vrPlayer = m_Game->m_PlayersVRInfo[index];

			if (m_VR->m_IsVREnabled && localIndex == index) {
				vNewTraceStart = m_VR->GetRightControllerAbsPos();
				vNewDirection = m_VR->m_RightControllerForward;
			}
			else if (vrPlayer.isUsingVR)
			{
				vNewTraceStart = vrPlayer.controllerPos;
				Vector fwd, rt, up;
				QAngle::AngleVectors(vrPlayer.controllerAngle, &fwd, &rt, &up);
				vNewDirection = fwd;
			}
		}
	}

	return hkTraceFirePortal.fOriginal(ecx, vNewTraceStart, vNewDirection, bPortal2, iPlacedBy, tr);
}

void __fastcall Hooks::dPlayerPortalled(void* ecx, void* edx, void* a2, __int64 a3)
{
	CBaseEntity* pBaseEntity = (CBaseEntity*)ecx;

	QAngle angAbsRotationBefore;
	m_Game->m_EngineClient->GetViewAngles(angAbsRotationBefore);

	hkPlayerPortalled.fOriginal(ecx, a2, a3);

	QAngle angAbsRotationAfter;
	m_Game->m_EngineClient->GetViewAngles(angAbsRotationAfter);

	if (angAbsRotationBefore != angAbsRotationAfter) {
		m_VR->m_PortalRotationOffset = angAbsRotationAfter - angAbsRotationBefore;
		m_VR->m_ApplyPortalRotationOffset = true;
	}

	return;
}

double __fastcall Hooks::dComputeError(void* ecx, void* edx) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	double computedError = hkComputeError.fOriginal(edx);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return computedError;
}

bool __fastcall Hooks::dUpdateObject(void* ecx, void* edx, void* pPlayer, float flError, bool bIsTeleport) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	bool value = hkUpdateObject.fOriginal(ecx, pPlayer, flError, bIsTeleport);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;

	return value;
}

// This function is apparently not used by Portal 2, remove?
void __fastcall Hooks::dRotateObject(void* ecx, void* edx, void* pPlayer, float fRotAboutUp, float fRotAboutRight, bool bUseWorldUpInsteadOfPlayerUp) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	hkRotateObject.fOriginal(ecx, pPlayer, fRotAboutUp, fRotAboutRight, bUseWorldUpInsteadOfPlayerUp);

	if (!wasTrue)
		m_VR->m_OverrideEyeAngles = false;
}

// This is CPlayerBase, do we also need to hook CPortalPlayer? can the same function be used by both?
// This works for release, but why was it crashing before??? TODO: buy a c++ book...
QAngle& __fastcall Hooks::dEyeAngles(void* ecx, void* edx) {
	if (m_VR->m_OverrideEyeAngles) {
		int localIndex = m_Game->m_EngineClient->GetLocalPlayer();
		int index = EntityIndex(ecx);

		auto vrPlayer = m_Game->m_PlayersVRInfo[index];

		if (m_VR->m_IsVREnabled && localIndex == index) {
			return m_VR->GetRightControllerAbsAngleConst();
		}
		else if (vrPlayer.isUsingVR)
		{
			return vrPlayer.controllerAngle;
		}
	}

	return hkEyeAngles.fOriginal(ecx);
}

int __fastcall Hooks::dGetDefaultFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

double __fastcall Hooks::dGetFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

double __fastcall Hooks::dGetViewModelFOV(void* ecx, void* edx) {
	return m_VR->m_Fov;
}

//Panel capture
void __fastcall Hooks::dPaintTraverse(void* ecx, void* edx, VPANEL vguiPanel, bool forceRepaint, bool allowForce)
{
#ifdef PrintTraverseNames
	printf("%s\n", m_Game->m_VguiIPanel->GetName(vguiPanel));
#endif

	static bool ResetSurface;
	if (!m_VR->m_BuiltCaptureMap)
		m_VR->BuildCaptureMap();

	VPANEL p = vguiPanel;
	auto it = m_VR->m_PanelCaptureMap.end();
	while (p)
	{
#ifdef PrintCompositerTraverseal
		printf("%s, ID: %d -> %s, ID: %d\n", m_Game->m_VguiIPanel->GetName(vguiPanel), vguiPanel, m_Game->m_VguiIPanel->GetName(p), p);
		printf("%d\n", m_VR->FindParentOf(m_Game->m_EnginePanel->GetPanel(PANEL_CLIENTDLL), "HudWeapon"));
#endif

		it = m_VR->m_PanelCaptureMap.find(p);
		if (it != m_VR->m_PanelCaptureMap.end())
			break;

		p = m_Game->m_VguiIPanel->GetParent(p);
	}

	if (it != m_VR->m_PanelCaptureMap.end() && it->second.m_ShouldCapture())
	{
		ITexture* OverrideTexture = it->second.m_ITex;
		bool excluded = IsPanelExcluded(vguiPanel, &it->second.m_ExcludePanel, OverrideTexture);

		if (!excluded)
		{
			IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
			rndrContext->SetRenderTarget(OverrideTexture);
			rndrContext->OverrideAlphaWriteEnable(true, true);

			hkPaintTraverse.fOriginal(ecx, vguiPanel, forceRepaint, allowForce);

			rndrContext->OverrideAlphaWriteEnable(false);
			rndrContext->Release();
			ResetSurface = true;
			return;
		}
	}
	else if (ResetSurface)
	{
		IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
		rndrContext->SetRenderTarget(NULL);
		ResetSurface = false;
		rndrContext->Release();
	}

	hkPaintTraverse.fOriginal(ecx, vguiPanel, forceRepaint, allowForce);
}

bool __fastcall Hooks::dLevelInit(void* ecx, void* edx, const char* pMapName, char const* pMapEntities, char const* pOldLevel, char const* pLandmarkName, bool loadGame, bool background)
{
	if (m_Game->m_VRDebuglvl) 
		m_Game->logMsg(LOGTYPE_DEBUG, "Loading level: %s, Background: %s", pMapName, (background) ? "True" : "False");

	if (m_VR->m_3DMenu)
		m_VR->m_IsLevelBackground = background;
	
	m_FirstFrame = true;
	m_VR->m_ParticleCreated = false; //Need to recache particle
	return hkLevelInit.fOriginal(ecx, pMapName, pMapEntities, pOldLevel, pLandmarkName, loadGame, background);
}

void __fastcall Hooks::dPrepareCredits(void* ecx, void* edx, const char* pKeyName)
{
	m_VR->m_IsCredits = true;
	hkPrepareCredits.fOriginal(ecx, pKeyName);
}

void __fastcall Hooks::dComputeShadowDepthTextures(void* ecx, void* edx, const CViewSetup& pView)
{
	if (m_VR->m_IsRightEye && m_VR->m_ExperimentalOptimizations)
		return;
		
	hkComputeShadowDepthTextures.fOriginal(ecx, pView);
}

void __fastcall Hooks::dUnlockAllShadowDepthTextures(void* ecx, void* edx)
{
	if (!m_VR->m_IsRightEye && m_VR->m_ExperimentalOptimizations)
		return;

	hkUnlockAllShadowDepthTextures.fOriginal(ecx);
}

void __fastcall Hooks::dPostActionSignal(void* ecx, void* edx, KeyValues* message)
{
#ifdef PanelCommands
	printf("PostActionSignal: %s, %s\n", message->GetName(), message->GetString(message->GetName(), ""));
#endif

	if (!strcmp(message->GetName(), "Command")) 
	{
		const char* cmd = message->GetString("command", "");
		auto it = m_VR->m_PanelCommands.find(cmd);
		if (it != m_VR->m_PanelCommands.end())
		{
			if (it->second(cmd, reinterpret_cast<Panel*>(ecx), message))
				return;
		}
	}

	hkPostActionSignal.fOriginal(ecx, message);
}

void __fastcall Hooks::dLoadControlSettings(void* ecx, void* edx, const char* dialogResourceName, const char* pathID, KeyValues* pPreloadedKeyValues, KeyValues* pConditions)
{
	std::string input = ToLower(dialogResourceName);
	size_t pos = input.find_last_of("/\\");
	std::string filePath = (pos == std::string::npos) ? input : input.substr(pos + 1);

	auto it = m_VR->m_PanelLayoutOverride.find(filePath);
	if (it != m_VR->m_PanelLayoutOverride.end())
	{
		OverrideLayout& layout = it->second;
		std::string path = layout.NewLayoutPath;
		if (layout.m_Func(path)) 
		{
			pPreloadedKeyValues = NULL;
			dialogResourceName = path.c_str();
		}
	}

#ifdef ResFiles
	const char* Key = "";
	if (pPreloadedKeyValues)
		Key = pPreloadedKeyValues->GetName();

	printf("LoadControlSettings: %s, %s\n", dialogResourceName, Key);
#endif

	hkLoadControlSettings.fOriginal(ecx, dialogResourceName, pathID, pPreloadedKeyValues, pConditions);
}

void __fastcall Hooks::dApplySettings(void* ecx, void* edx, KeyValues* inResourceData)
{
	Panel* pan = reinterpret_cast<Panel*>(ecx);
#ifdef PrintPanelSettings
	printf("ApplySettings: %s\n", pan->m_PanelName);
#endif

	auto it = m_VR->m_PanelSettings.find(pan->m_PanelName);
	if (it != m_VR->m_PanelSettings.end())
	{
		PanelSettings& data = it->second;
		if (data.m_Func(pan, inResourceData, data.m_Data))
			return;
	}

	hkApplySettings.fOriginal(ecx, inResourceData);
}

float __fastcall Hooks::dUpdateProgressBar(void* ecx, void* edx)
{
	float Percentage = hkUpdateProgressBar.fOriginal(ecx);

	Panel* pan = reinterpret_cast<Panel*>(ecx);
	auto it = m_VR->m_SlideRead.find(pan->m_PanelName);
	if (it != m_VR->m_SlideRead.end())
		it->second(pan, Percentage);

	return Percentage;
}

//This fixes viewmodel attachment drift
void Hooks::dFormatViewModelAttachment(void* param_1, Vector& vOrigin, bool bInverse)
{
	//hkFormatViewModelAttachment.fOriginal(param_1, vOrigin, bInverse);
}