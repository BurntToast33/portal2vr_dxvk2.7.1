#include "hooks.h"
#include "game.h"
#include "texture.h"
#include "sdk.h"
#include "sdk_server.h"
#include "vr.h"
#include "offsets.h"
#include <iostream>


//#define PrintTraverseNames


Hooks::Hooks(Game *game)
{
	if (MH_Initialize() != MH_OK)
	{
		Game::errorMsg("Failed to init MinHook");
	}

	m_Game = game;
	m_VR = m_Game->m_VR;

	m_PushHUDStep = -999;
	m_PushedHud = true;

#ifdef OVERRIDEVRMODE
	return;
#endif


	BuildHook(&hkPrepareCredits, &m_Game->m_Offsets->PrepareCredits, &dPrepareCredits);

	// Movement
	BuildHook(&hkProcessUsercmds, &m_Game->m_Offsets->ProcessUsercmds, &dProcessUsercmds);
	BuildHook(&hkReadUsercmd, &m_Game->m_Offsets->ReadUserCmd, &dReadUsercmd);
	BuildHook(&hkWriteUsercmd, &m_Game->m_Offsets->WriteUsercmd, &dWriteUsercmd);

	// Weapon
	BuildHook(&hkWeapon_ShootPosition, &m_Game->m_Offsets->Weapon_ShootPosition, &dWeapon_ShootPosition);
	BuildHook(&hkTraceFirePortal, &m_Game->m_Offsets->TraceFirePortalServer, &dTraceFirePortal);
	BuildHook(&hkCWeaponPortalgun_FirePortal, &m_Game->m_Offsets->CWeaponPortalgun_FirePortal, &dCWeaponPortalgun_FirePortal);

	// Rendering
	BuildHook(&hkRenderView, &m_Game->m_Offsets->RenderView, &dRenderView);
	BuildHook(&hkPaintTraverse, &m_Game->m_Offsets->VGui_IPanel_PaintTraverse, &dPaintTraverse);
	BuildHook(&hkCalcViewModelView, &m_Game->m_Offsets->CalcViewModelView, &dCalcViewModelView);
	BuildHook(&hkDrawSelf, &m_Game->m_Offsets->DrawSelf, &dDrawSelf);
	BuildHook(&hkClipTransform, &m_Game->m_Offsets->ClipTransform, &dClipTransform, false);
	//BuildHook(&hkVgui_Paint, &m_Game->m_Offsets->VGui_Paint, &dVGui_Paint, false);
	//BuildHook(&hkPushRenderTargetAndViewport, &m_Game->m_Offsets->PushRenderTargetAndViewport, &dPushRenderTargetAndViewport, false);
	//BuildHook(&hkPopRenderTargetAndViewport, &m_Game->m_Offsets->PopRenderTargetAndViewport, &dPopRenderTargetAndViewport, false);
	//BuildHook(&hkPrePushRenderTarget, &m_Game->m_Offsets->PrePushRenderTarget, &dPrePushRenderTarget, false);


	// Portalling
	BuildHook(&hkPlayerPortalled, &m_Game->m_Offsets->PlayerPortalled, &dPlayerPortalled);
	BuildHook(&hkCreateMove, &m_Game->m_Offsets->CreateMove, &dCreateMove);
	UTIL_Portal_FirstAlongRay = (tUTIL_Portal_FirstAlongRay)m_Game->m_Offsets->UTIL_Portal_FirstAlongRay.address;
	UTIL_IntersectRayWithPortal = (tUTIL_IntersectRayWithPortal)m_Game->m_Offsets->UTIL_IntersectRayWithPortal.address;
	UTIL_Portal_AngleTransform = (tUTIL_Portal_AngleTransform)m_Game->m_Offsets->UTIL_Portal_AngleTransform.address;

	// Grababbles
	BuildHook(&hkComputeError, &m_Game->m_Offsets->ComputeError, &dComputeError, false);
	BuildHook(&hkUpdateObject, &m_Game->m_Offsets->UpdateObject, &dUpdateObject);
	BuildHook(&hkUpdateObjectVM, &m_Game->m_Offsets->UpdateObjectVM, &dUpdateObjectVM);
	BuildHook(&hkRotateObject, &m_Game->m_Offsets->RotateObject, &dRotateObject, false);
	BuildHook(&hkEyeAngles, &m_Game->m_Offsets->EyeAngles, &dEyeAngles);

	// Portal Gun VFX
	BuildHook(&hkGetDefaultFOV, &m_Game->m_Offsets->GetDefaultFOV, &dGetDefaultFOV);
	BuildHook(&hkGetFOV, &m_Game->m_Offsets->GetFOV, &dGetFOV);
	BuildHook(&hkGetViewModelFOV, &m_Game->m_Offsets->GetViewModelFOV, &dGetViewModelFOV);

	// Laser Pointer
	BuildHook(&hkSetDrawOnlyForSplitScreenUser, &m_Game->m_Offsets->SetDrawOnlyForSplitScreenUser, &dSetDrawOnlyForSplitScreenUser);
	BuildHook(&hkCHudCrosshair_ShouldDraw, &m_Game->m_Offsets->CHudCrosshair_ShouldDraw, &dCHudCrosshair_ShouldDraw);
	GetPortalPlayer = (tGetPortalPlayer)m_Game->m_Offsets->GetPortalPlayer.address;
	CreatePingPointer = (tCreatePingPointer)m_Game->m_Offsets->CreatePingPointer.address;
	PrecacheParticleSystem = (tPrecacheParticleSystem)m_Game->m_Offsets->PrecacheParticleSystem.address;

	//
	EntityIndex = (tEntindex)m_Game->m_Offsets->CBaseEntity_entindex.address;
	GetOwner = (tGetOwner)m_Game->m_Offsets->GetOwner.address;
	GetFullScreenTexture = (tGetFullScreenTexture)m_Game->m_Offsets->GetFullScreenTexture.address;

	//Map realted
	BuildHook(&hkLevelInit, &m_Game->m_Offsets->LevelInit, &dLevelInit);
}

Hooks::~Hooks()
{
	if (MH_Uninitialize() != MH_OK)
		Game::errorMsg("Failed to uninitialize MinHook");
}

bool __fastcall Hooks::dCHudCrosshair_ShouldDraw(void* ecx, void* edx) {
	bool shouldDraw = hkCHudCrosshair_ShouldDraw.fOriginal(ecx);

	m_VR->m_DrawCrosshair = shouldDraw;

	return ((m_VR->m_AimMode == 1) ? shouldDraw : false);
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
		PrecacheParticleSystem("robot_point_beam");
		m_VR->FirstFrameUpdate();
		m_FirstFrame = false;
	}

	hudViewSetup.width = m_VR->m_RenderWidth;
	hudViewSetup.height = m_VR->m_RenderHeight;
	hudViewSetup.m_nUnscaledWidth = m_VR->m_RenderWidth;
	hudViewSetup.m_nUnscaledHeight = m_VR->m_RenderHeight;
	hudViewSetup.fov = m_VR->m_Fov;
	hudViewSetup.fovViewmodel = m_VR->m_Fov;
	hudViewSetup.m_flAspectRatio = m_VR->m_Aspect;

	Vector position = setup.origin;

	if (m_VR->m_ApplyPortalRotationOffset) {
		float distance = (setup.origin - m_VR->m_SetupOrigin).LengthSqr();

		// Rudimentary portalling detection
		if (distance > 35) {
			//m_VR->m_RotationOffset.x += m_VR->m_PortalRotationOffset.x;
			m_VR->m_RotationOffset.y += m_VR->m_PortalRotationOffset.y;
			//m_VR->m_RotationOffset.z += m_VR->m_PortalRotationOffset.z;

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
		int drawFlags = (!I) ? (whatToDraw & ~RENDERVIEW_DRAWHUD) : whatToDraw;
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

	rndrContext->SetRenderTarget(NULL);
	rndrContext->Release();

	m_PushedHud = false;
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

	//std::cout << "dCalcViewModelView: (" << m_VR->m_IsVREnabled << ")\n";

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

int Hooks::dGetPrimaryAttackActivity(void *ecx, void *edx, void *meleeInfo)
{
	return hkGetPrimaryAttackActivity.fOriginal(ecx, meleeInfo);
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

void Hooks::dPushRenderTargetAndViewport(void *ecx, void *edx, ITexture *pTexture, ITexture *pDepthTexture, int nViewX, int nViewY, int nViewW, int nViewH)
{
	if (m_VR->m_CreatedVRTextures && !m_PushedHud)
	{
		//pTexture = m_VR->m_HUD.m_ITex;

		////pTexture = m_VR->m_RightEyeTexture;

		//IMatRenderContext *renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		//renderContext->ClearBuffers(false, true, true);
		////renderContext->Release();

		//hkPushRenderTargetAndViewport.fOriginal(ecx, pTexture, pDepthTexture, nViewX, nViewY, nViewW, nViewH);

		////renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		//renderContext->OverrideAlphaWriteEnable(true, true);
		//renderContext->ClearColor4ub(0, 0, 0, 0);
		//renderContext->ClearBuffers(true, false);
		//renderContext->Release();

		//m_VR->m_RenderedHud = true;
		//m_PushedHud = true;
	}
	else
	{
		hkPushRenderTargetAndViewport.fOriginal(ecx, pTexture, pDepthTexture, nViewX, nViewY, nViewW, nViewH);
	}
}

void Hooks::dPopRenderTargetAndViewport(void *ecx, void *edx)
{
	if (!m_VR->m_CreatedVRTextures)
		return hkPopRenderTargetAndViewport.fOriginal(ecx);

	//std::cout << "dPopRenderTargetAndViewport: " << m_PushHUDStep << "\n";

	m_PushHUDStep = 0;

	if (m_PushedHud)
	{
		IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
		renderContext->OverrideAlphaWriteEnable(false, true);
		renderContext->ClearColor4ub(0, 0, 0, 255);
		renderContext->Release();
	}

	hkPopRenderTargetAndViewport.fOriginal(ecx);
}

void Hooks::dVGui_Paint(void *ecx, void *edx, int mode)
{
	if (!m_VR->m_CreatedVRTextures || m_VR->m_Game->m_VguiSurface->IsCursorVisible())
		return hkVgui_Paint.fOriginal(ecx, mode);

	if (m_PushedHud)
		mode = PAINT_UIPANELS | PAINT_INGAMEPANELS;

	hkVgui_Paint.fOriginal(ecx, mode);
}

int Hooks::dIsSplitScreen()
{
	//std::cout << "dIsSplitScreen: " << m_PushHUDStep << "\n";

	if (m_PushHUDStep == 0)
		++m_PushHUDStep;
	else
		m_PushHUDStep = -999;

	return hkIsSplitScreen.fOriginal();
}

DWORD *Hooks::dPrePushRenderTarget(void *ecx, void *edx, int a2)
{
	//std::cout << "dPrePushRenderTarget: " << m_PushHUDStep << "\n";

	if (m_PushHUDStep == 1)
		++m_PushHUDStep;
	else
		m_PushHUDStep = -999;

	return hkPrePushRenderTarget.fOriginal(ecx, a2);
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

//Dummy function
bool Hooks::dClipTransform(const Vector& point, Vector* pScreen)
{
	return hkClipTransform.fOriginal(point, pScreen);
}

bool Hooks::ScreenTransform(const Vector& point, Vector* pScreen, int width, int height)
{
	bool retval = hkClipTransform.fOriginal(point, pScreen);

	pScreen->x = 0.5f * (pScreen->x + 1.0f) * width;
	pScreen->y = 0.5f * (-pScreen->y + 1.0f) * height;

	return retval;
}

int __fastcall Hooks::dDrawSelf(void* ecx, void* edx, int x, int y, int w, int h, const void* clr, float flApparentZ) {
	//std::cout << "dDrawSelf - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << ", Z: " << flApparentZ << "\n";

	//int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();

	//auto viewport = m_Game->m_ClientMode->GetViewport();

	int newX = x;
	int	newY = y;

	if (m_VR->m_IsVREnabled)
	{
		Vector screen = { 0, 0, 0 };

		//Vector vec = m_VR->m_AimPos - m_VR->GetRightControllerAbsPos();

		//newZ = 1.0 / sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);

		ScreenTransform(m_VR->m_AimPos, &screen, m_VR->m_RenderWidth, m_VR->m_RenderHeight);

		int offsetX = x - (m_Game->m_WindowWidth * 0.5f);
		int offsetY = y - (m_Game->m_WindowHeight * 0.5f);

		newX = screen.x + offsetX;
		newY = screen.y + offsetY;
	}

	return hkDrawSelf.fOriginal(ecx, newX, newY, w, h, clr, flApparentZ);
}

void __cdecl Hooks::dVGui_GetHudBounds(int slot, int& x, int& y, int& w, int& h) {
	if (m_VR->m_IsVREnabled && !m_Game->m_VguiSurface->IsCursorVisible())
	{
		x = y = 0;
		w = m_VR->m_RenderWidth;
		h = m_VR->m_RenderHeight;
	} else {
		hkVGui_GetHudBounds.fOriginal(slot, x, y, w, h);
	}

	//std::cout << "dVGui_GetHudBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";
}

void __cdecl Hooks::dVGui_GetPanelBounds(int slot, int& x, int& y, int& w, int& h) {
	if (m_VR->m_IsVREnabled && !m_Game->m_VguiSurface->IsCursorVisible())
	{
		x = y = 0;
		w = m_VR->m_RenderWidth;
		h = m_VR->m_RenderHeight;
	}
	else {
		hkVGui_GetPanelBounds.fOriginal(slot, x, y, w, h);
	}

	//std::cout << "dVGui_GetPanelBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";
}

void __cdecl Hooks::dVGUI_UpdateScreenSpaceBounds(int nNumSplits, int sx, int sy, int sw, int sh) {
	hkVGUI_UpdateScreenSpaceBounds.fOriginal(nNumSplits, sx, sy, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
}

void __cdecl Hooks::dVGui_GetTrueScreenSize(int &w, int &h) {
	w = m_VR->m_RenderWidth;
	h = m_VR->m_RenderHeight;
}

void __fastcall Hooks::dGetScreenSize(void* ecx, void* edx, int& wide, int& tall) {
	//hkGetScreenSize.fOriginal(ecx, wide, tall);
	wide = m_VR->m_RenderWidth;
	tall = m_VR->m_RenderHeight;
}

void __cdecl Hooks::dGetHudSize(int& w, int& h) {
	w = m_VR->m_RenderWidth;
	h = m_VR->m_RenderHeight;
}

void __fastcall Hooks::dPush2DView(void* ecx, void* edx, IMatRenderContext* pRenderContext, const CViewSetup& view, int nFlags, ITexture* pRenderTarget, void* frustumPlanes) {
	m_PushedHud = false;

	return hkPush2DView.fOriginal(ecx, pRenderContext, view, nFlags, pRenderTarget, frustumPlanes);
}

void __fastcall Hooks::dRender(void* ecx, void* edx, vrect_t* rect) {
	//std::cout << "dRender - X: " << rect->x << ", Y: " << rect->y << ", W: " << rect->width << ", H: " << rect->height  << "\n";

	return hkRender.fOriginal(ecx, rect);
}

void __fastcall Hooks::dSetBounds(void* ecx, void* edx, int x, int y, int w, int h) {
	std::cout << "dSetBounds - X: " << x << ", Y: " << y << ", W: " << w << ", H: " << h << "\n";

	hkSetBounds.fOriginal(ecx, x, y, m_VR->m_RenderWidth, m_VR->m_RenderHeight);
}

void __fastcall Hooks::dGetClipRect(void* ecx, void* edx, int& x0, int& y0, int& x1, int& y1) {
	hkGetClipRect.fOriginal(ecx, x0, y0, x1, y1);

	//std::cout << "dGetClipRect - X: " << x0 << ", Y: " << y0 << ", W: " << x1 << ", H: " << y1  << "\n";
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

bool __fastcall Hooks::dUpdateObjectVM(void* ecx, void* edx, void* pPlayer, float flError) {
	bool wasTrue = m_VR->m_OverrideEyeAngles;

	m_VR->m_OverrideEyeAngles = true;

	bool value = hkUpdateObjectVM.fOriginal(ecx, pPlayer, flError);

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
	const char* Name = m_Game->m_VguiIPanel->GetName(vguiPanel);
	if (Name) std::cout << Name << std::endl;
#endif

	static bool ResetSurface = false;
	if (!m_VR->m_BuiltCaptureMap)
		m_VR->BuildCaptureMap();


	auto it = m_VR->m_PanelCaptureMap.find(m_Game->m_VguiIPanel->GetParent(vguiPanel));
	if (it != m_VR->m_PanelCaptureMap.end() && it->second.m_ShouldCapture())
	{
		ITexture* OverrideTexture = it->second.m_ITex;
		bool excluded = IsPanelExcluded(vguiPanel, it->second.m_ExcludePanel, OverrideTexture);

		if (!excluded) 
		{
			IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
			rndrContext->SetRenderTarget(OverrideTexture);
			rndrContext->OverrideColorWriteEnable(true, true);

			hkPaintTraverse.fOriginal(ecx, vguiPanel, forceRepaint, allowForce);

			rndrContext->OverrideColorWriteEnable(false, true);
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
	if (m_VR->m_3DMenu)
	{
		m_VR->m_StopLoading3DBgr = false;
		m_VR->m_IsLevelBackground = background;
	}
		
	m_FirstFrame = true;
	return hkLevelInit.fOriginal(ecx, pMapName, pMapEntities, pOldLevel, pLandmarkName, loadGame, background);
}

void __fastcall Hooks::dPrepareCredits(void* ecx, void* edx, const char* pKeyName)
{
	m_VR->m_IsCredits = true;
	hkPrepareCredits.fOriginal(ecx, pKeyName);
}