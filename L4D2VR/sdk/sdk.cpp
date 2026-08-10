#include "sdk.h"

int DefaultCompletionFunc(const char* partial, char commands[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH])
{
	return 0;
}

ConCommand::ConCommand(const char* pName, FnCommandCallbackVoid_t callback, const char* pHelpString /*= 0*/, int flags /*= 0*/, FnCommandCompletionCallback completionFunc /*= 0*/)
{
	// Set the callback
	m_fnCommandCallbackV1 = callback;
	m_bUsingNewCommandCallback = false;
	m_bUsingCommandCallbackInterface = false;
	m_fnCompletionCallback = completionFunc ? completionFunc : DefaultCompletionFunc;
	m_bHasCompletionCallback = completionFunc != 0 ? true : false;

	// Setup the rest
	BaseClass::CreateBase(pName, pHelpString, flags);
}

ConCommand::ConCommand(const char* pName, FnCommandCallback_t callback, const char* pHelpString /*= 0*/, int flags /*= 0*/, FnCommandCompletionCallback completionFunc /*= 0*/)
{
	// Set the callback
	m_fnCommandCallback = callback;
	m_bUsingNewCommandCallback = true;
	m_fnCompletionCallback = completionFunc ? completionFunc : DefaultCompletionFunc;
	m_bHasCompletionCallback = completionFunc != 0 ? true : false;
	m_bUsingCommandCallbackInterface = false;

	// Setup the rest
	BaseClass::CreateBase(pName, pHelpString, flags);
}

ConCommand::ConCommand(const char* pName, ICommandCallback* pCallback, const char* pHelpString /*= 0*/, int flags /*= 0*/, ICommandCompletionCallback* pCompletionCallback /*= 0*/)
{
	// Set the callback
	m_pCommandCallback = pCallback;
	m_bUsingNewCommandCallback = false;
	m_pCommandCompletionCallback = pCompletionCallback;
	m_bHasCompletionCallback = (pCompletionCallback != 0);
	m_bUsingCommandCallbackInterface = true;

	// Setup the rest
	BaseClass::CreateBase(pName, pHelpString, flags);
}

ConCommand::~ConCommand(void)
{
}

bool ConCommand::IsCommand(void) const
{
	return true;
}

void	ConCommand::AutoCompleteSuggestStub()
{
}

bool ConCommand::CanAutoComplete(void)
{
	return m_bHasCompletionCallback;
}

void ConCommand::Dispatch(const CCommand& command)
{
	if (m_bUsingNewCommandCallback)
	{
		if (m_fnCommandCallback)
		{
			(*m_fnCommandCallback)(command);
			return;
		}
	}
	else if (m_bUsingCommandCallbackInterface)
	{
		if (m_pCommandCallback)
		{
			m_pCommandCallback->CommandCallback(command);
			return;
		}
	}
	else
	{
		if (m_fnCommandCallbackV1)
		{
			(*m_fnCommandCallbackV1)();
			return;
		}
	}

	// Command without callback!!!
	AssertMsg(0, "Encountered ConCommand '%s' without a callback!\n", GetName());
}

void CharacterSetBuild(characterset_t* pSetBuffer, const char* pszSetString)
{
	int i = 0;

	// Test our pointers
	if (!pSetBuffer || !pszSetString)
		return;

	memset(pSetBuffer->set, 0, sizeof(pSetBuffer->set));

	while (pszSetString[i])
	{
		pSetBuffer->set[(unsigned)pszSetString[i]] = 1;
		i++;
	}

}

static characterset_t s_BreakSet;
static bool s_bBuiltBreakSet = false;

CCommand::CCommand()
{
	if (!s_bBuiltBreakSet)
	{
		s_bBuiltBreakSet = true;
		CharacterSetBuild(&s_BreakSet, "{}()':");
	}

	Reset();
}

CCommand::CCommand(int nArgC, const char** ppArgV)
{
}

void CCommand::Reset()
{
	m_nArgc = 0;
	m_nArgv0Size = 0;
	m_pArgSBuffer[0] = 0;
}

void CCommand::TokenizeStub(const char* pCommand, characterset_t* pBreakSet)
{
}

ConCommandBase::ConCommandBase(void)
{
	m_bRegistered = false;
	m_pszName = NULL;
	m_pszHelpString = NULL;

	m_nFlags = 0;
	m_pNext = NULL;
}

ConCommandBase::ConCommandBase(const char* pName, const char* pHelpString /*=0*/, int flags /*= 0*/)
{
	CreateBase(pName, pHelpString, flags);
}

ConCommandBase::~ConCommandBase(void)
{
}

bool ConCommandBase::IsCommand(void) const
{
	return true;
}

bool ConCommandBase::IsFlagSet(int flag) const
{
	return (flag & m_nFlags) ? true : false;
}

void ConCommandBase::AddFlags(int flags)
{
	m_nFlags |= flags;
}

const char* ConCommandBase::GetName(void) const
{
	return m_pszName;
}

const char* ConCommandBase::GetHelpText(void) const
{
	return m_pszHelpString;
}

const ConCommandBase* ConCommandBase::GetNext(void) const
{
	return m_pNext;
}

ConCommandBase* ConCommandBase::GetNext(void)
{
	return m_pNext;
}

bool ConCommandBase::IsRegistered(void) const
{
	return m_bRegistered;
}

void ConCommandBase::GetDLLIdentifierStub()
{
}

void ConCommandBase::CreateBase(const char* pName, const char* pHelpString /*= 0*/, int flags /*= 0*/)
{
	m_bRegistered = false;

	// Name should be static data
	m_pszName = pName;
	m_pszHelpString = pHelpString ? pHelpString : "";

	m_nFlags = flags;

	if (!(m_nFlags & FCVAR_UNREGISTERED))
	{
		m_pNext = s_pConCommandBases;
		s_pConCommandBases = this;
	}
	else
	{
		// It's unregistered
		m_pNext = NULL;
	}

	// If s_pAccessor is already set (this ConVar is not a global variable),
	//  register it.
	if (s_pAccessor)
	{
		Init();
	}
}

void ConCommandBase::Init()
{
	if (s_pAccessor)
	{
		s_pAccessor->RegisterConCommandBase(this);
	}
}

void ConCommandBase::Shutdown()
{
	if (g_Game->m_ICvar)
	{
		g_Game->m_ICvar->UnregisterConCommand(this);
	}
}

void ConCommandBase::CopyStringSub()
{
}

ConCommandBase* ConCommandBase::s_pConCommandBases = nullptr;
IConCommandBaseAccessor* ConCommandBase::s_pAccessor = nullptr;

FileWeaponInfo_t::FileWeaponInfo_t()
{
	bParsedScript = false;
	bLoadedHudElements = false;
	szClassName[0] = 0;
	szPrintName[0] = 0;

	szViewModel[0] = 0;
	szWorldModel[0] = 0;
	szAnimationPrefix[0] = 0;
	iSlot = 0;
	iPosition = 0;
	iMaxClip1 = 0;
	iMaxClip2 = 0;
	iDefaultClip1 = 0;
	iDefaultClip2 = 0;
	iWeight = 0;
	iRumbleEffect = -1;
	bAutoSwitchTo = false;
	bAutoSwitchFrom = false;
	iFlags = 0;
	szAmmo1[0] = 0;
	szAmmo2[0] = 0;
	memset(aShootSounds, 0, sizeof(aShootSounds));
	iAmmoType = 0;
	iAmmo2Type = 0;
	m_bMeleeWeapon = false;
	iSpriteCount = 0;
	iconActive = 0;
	iconInactive = 0;
	iconAmmo = 0;
	iconAmmo2 = 0;
	iconCrosshair = 0;
	iconAutoaim = 0;
	iconZoomedCrosshair = 0;
	iconZoomedAutoaim = 0;
	bShowUsageHint = false;
	m_bAllowFlipping = true;
	m_bBuiltRightHanded = true;
}