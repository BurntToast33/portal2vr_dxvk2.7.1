#include "luamanager.h"
#include "sdk.h"

extern "C"
{
    #include "src/lua.h"
    #include "src/lauxlib.h"
    #include "src/lualib.h"
}

LuaManager::LuaManager(Game* game) : m_Game(game) 
{
    m_State = luaL_newstate();

    if (!m_State)
    {
        m_Game->logMsg(LOGTYPE_ERROR, "LuaManager: Failed to create Lua state.");
        return;
    }

    luaL_openlibs(m_State);

    lua_pushlightuserdata(m_State, this);
    lua_setfield(m_State, LUA_REGISTRYINDEX, "LuaManager");
}

LuaManager::~LuaManager() 
{
    Shutdown();
}

void LuaManager::Initialize() 
{
    if (!m_State)
    {
        m_Game->logMsg(LOGTYPE_ERROR, "LuaManager: Cannot initialize without a Lua state.");
        return;
    }

    if (m_Initialized) return;

#pragma region Registration
    //Logging
    lua_newtable(m_State);
    lua_pushcfunction(m_State, Lua_GameLog);
    lua_setfield(m_State, -2, "log");
    lua_setglobal(m_State, "game");

    //Panel roots
    lua_newtable(m_State);

    lua_pushinteger(m_State, PANEL_ROOT);
    lua_setfield(m_State, -2, "ROOT");

    lua_pushinteger(m_State, PANEL_GAMEUIDLL);
    lua_setfield(m_State, -2, "GAMEUIDLL");

    lua_pushinteger(m_State, PANEL_CLIENTDLL);
    lua_setfield(m_State, -2, "CLIENTDLL");

    lua_pushinteger(m_State, PANEL_TOOLS);
    lua_setfield(m_State, -2, "TOOLS");

    lua_pushinteger(m_State, PANEL_INGAMESCREENS);
    lua_setfield(m_State, -2, "INGAMESCREENS");

    lua_pushinteger(m_State, PANEL_GAMEDLL);
    lua_setfield(m_State, -2, "GAMEDLL");

    lua_pushinteger(m_State, PANEL_CLIENTDLL_TOOLS);
    lua_setfield(m_State, -2, "CLIENTDLL_TOOLS");

    lua_pushinteger(m_State, PANEL_SIZING);
    lua_setfield(m_State, -2, "SIZING");

    lua_setglobal(m_State, "VGuiPanel");
#pragma endregion

    if (!ExecuteFile("Hud.lua"))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "LuaManager: Failed to initialize Hud.lua.");
        return;
    }

    m_Initialized = true;
    m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Lua initialized.");
}

void LuaManager::Shutdown()
{
    if (!m_State) return;

    lua_close(m_State);
    m_State = nullptr;

    m_Initialized = false;
    m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Lua shutdown.");
}

bool LuaManager::ExecuteFile(const char* filename)
{
    if (!m_State) return false;
    std::string path = std::string(m_Game->m_GameDir) + "\\VR\\Scripts\\" + m_Game->m_GameType.first + filename;

    if (luaL_dofile(m_State, path.c_str()) != LUA_OK)
    {
        const char* error = lua_tostring(m_State, -1);
        Game::logMsg(LOGTYPE_ERROR, "Lua: Failed to load '%s': %s", filename, error ? error : "Unknown error");

        lua_pop(m_State, 1);
        return false;
    }

    return true;
}


int LuaManager::Lua_GameLog(lua_State* state)
{
    const char* message = luaL_checkstring(state, 1);
    Game::logMsg(LOGTYPE_LUA, "%s", message);
    return 0;
}

void LuaManager::Lua_PreUpdate()
{
    if (!m_State || !m_Initialized)
        return;

    lua_getglobal(m_State, "PreUpdate");

    if (!lua_isfunction(m_State, -1))
    {
        lua_pop(m_State, 1);
        return;
    }

    if (lua_pcall(m_State, 0, 0, 0) != LUA_OK)
    {
        const char* error = lua_tostring(m_State, -1);
        Game::logMsg(LOGTYPE_LUA, "Lua PreUpdate error: %s", error ? error : "Unknown error");
        lua_pop(m_State, 1);
    }
}

void LuaManager::Lua_TextureMapping()
{
    if (!m_State || !m_Initialized)
        return;

    lua_getglobal(m_State, "TextureMapping");

    if (!lua_isfunction(m_State, -1))
    {
        lua_pop(m_State, 1);
        return;
    }

    if (lua_pcall(m_State, 0, 0, 0) != LUA_OK)
    {
        const char* error = lua_tostring(m_State, -1);
        Game::logMsg(LOGTYPE_LUA, "Lua TextureMapping error: %s", error ? error : "Unknown error");
        lua_pop(m_State, 1);
    }
}
