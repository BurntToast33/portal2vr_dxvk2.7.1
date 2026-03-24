#pragma once

#include <math.h>
#include "game.h"
#include "offsets.h"

enum ParticleAttachment_t
{
	PATTACH_ABSORIGIN = 0,			// Create at absorigin, but don't follow
	PATTACH_ABSORIGIN_FOLLOW,		// Create at absorigin, and update to follow the entity
	PATTACH_CUSTOMORIGIN,			// Create at a custom origin, but don't follow
	PATTACH_POINT,					// Create on attachment point, but don't follow
	PATTACH_POINT_FOLLOW,			// Create on attachment point, and update to follow the entity

	PATTACH_WORLDORIGIN,			// Used for control points that don't attach to an entity

	PATTACH_ROOTBONE_FOLLOW,		// Create at the root bone of the entity, and update to follow

	MAX_PATTACH_TYPES,
};


class Game;
class CParticleSystemDefinition;
class C_BaseEntity;

class CNewParticleEffect
{
public:
	inline void SetControlPoint(int nWhichPoint, const Vector& v) {
		typedef int(__thiscall* tSetControlPoint)(void* thisptr, int nWhichPoint, const Vector& v);
		static tSetControlPoint oSetControlPoint = (tSetControlPoint)(g_Game->m_Offsets->SetControlPoint.address);

		oSetControlPoint(this, nWhichPoint, v);
	};

	inline void StopEmission(bool bInfiniteOnly = false, bool bRemoveAllParticles = false, bool bWakeOnStop = false, bool bPlayEndCap = false) {
		typedef void(__thiscall* tStopEmission)(void* thisptr, bool bInfiniteOnly, bool bRemoveAllParticles, bool bWakeOnStop, bool bPlayEndCap);
		static tStopEmission oStopEmission = (tStopEmission)(g_Game->m_Offsets->StopEmission.address);

		oStopEmission(this, bInfiniteOnly, bRemoveAllParticles, bWakeOnStop, bPlayEndCap);
	};

	inline void StartEmission(bool bInfiniteOnly = false) {
		typedef void(__thiscall* tStartEmission)(void* thisptr, bool bInfiniteOnly);
		static tStartEmission oStartEmission = (tStartEmission)(g_Game->m_Offsets->StartEmission.address);

		oStartEmission(this, bInfiniteOnly);
	};
};

class CPortal_Base2D
{
public:
	inline VMatrix MatrixThisToLinked() {
		return *(VMatrix*)((uintptr_t)this + 0x4C4);
	};
};