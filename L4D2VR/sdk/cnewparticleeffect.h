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

template<typename Fn, typename... Args>
decltype(auto) CallVFunc(void* instance, std::size_t index, Args&&... args)
{
	auto** vtable = *reinterpret_cast<void***>(instance);
	Fn fn = reinterpret_cast<Fn>(vtable[index]);

	if constexpr (sizeof...(Args) == 0)
		return fn(instance);
	else
		return fn(instance, std::forward<Args>(args)...);
}

template<typename Fn, typename... Args>
decltype(auto) CallFunction(std::uintptr_t address, Args&&... args)
{
	return reinterpret_cast<Fn>(address)(std::forward<Args>(args)...);
}

class Game;
class CParticleSystemDefinition;
class C_BaseEntity;

#define DestroyParticle(x) \
    if (x) {            \
        x->StopEmission(false, true);   \
        x = nullptr;    \
    }

class CNewParticleEffect
{
public:
	inline void SetControlPoint(int nWhichPoint, const Vector& v) {
		using tSetControlPoint = int(__thiscall*)(void* thisptr, int nWhichPoint, const Vector& v);
		CallFunction<tSetControlPoint>(g_Game->m_Offsets->SetControlPoint.address, this, nWhichPoint, v);
	};

	inline void StopEmission(bool bInfiniteOnly = false, bool bRemoveAllParticles = false, bool bWakeOnStop = false, bool bPlayEndCap = false) {
		using tStopEmission = void(__thiscall*)(void* thisptr, bool bInfiniteOnly, bool bRemoveAllParticles, bool bWakeOnStop, bool bPlayEndCap);
		CallFunction<tStopEmission>(g_Game->m_Offsets->StopEmission.address, this, bInfiniteOnly, bRemoveAllParticles, bWakeOnStop, bPlayEndCap);
	};
};

class CPortal_Base2D
{
public:
	inline VMatrix MatrixThisToLinked() {
		return *(VMatrix*)((uintptr_t)this + 0x4C4);
	};
};