#pragma once
#include "game.h"

struct lua_State;

class LuaManager
{
	Game* m_Game = nullptr;
	lua_State* m_State = nullptr;
	bool m_Initialized = false;

public:
	LuaManager(Game* game);
	~LuaManager();

	void Initialize();
	void Shutdown();
	bool ExecuteFile(const char* filename);

	//Mapped functions
	void Lua_PreUpdate();
	void Lua_TextureMapping();

private:
	static int Lua_GameLog(lua_State* state);
};
