#include "luamanager.h"
#include "vr.h"

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

    m_WorkingDir = std::string(m_Game->m_GameDir) + "\\VR\\Scripts\\" + m_Game->m_GameType.first + "\\";
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
    Scope("Game", [&]()
    {
        RegisterFunction(Lua_GetPanel, "GetPanel");
        RegisterFunction(Lua_FindParentOf, "FindParentOf");
        RegisterFunction(Lua_IsInGame, "IsInGame");
    });

    Scope("VR", [&]()
    {
        RegisterEnum(VRBindingType_None, "VRBINDINGTYPE_NONE");
        RegisterEnum(VRBindingType_Input, "VRBINDINGTYPE_INPUT");
        RegisterEnum(VRBindingType_Menu, "VRBINDINGTYPE_MENU");
        RegisterEnum(VRBindingType_Analog, "VRBINDINGTYPE_ANALOG");

        RegisterEnum(VRBindingMode_Button, "VRBINDINGMODE_BUTTON");
        RegisterEnum(VRBindingMode_Toggle, "VRBINDINGMODE_TOGGLE");
        RegisterEnum(VRBindingMode_Hold, "VRBINDINGMODE_HOLD");
        RegisterEnum(VRBindingMode_Repeat, "VRBINDINGMODE_REPEAT");

        //Mouse buttons
        RegisterEnum(VK_LBUTTON, "VK_LBUTTON");
        RegisterEnum(VK_RBUTTON, "VK_RBUTTON");
        RegisterEnum(VK_MBUTTON, "VK_MBUTTON");
        RegisterEnum(VK_XBUTTON1, "VK_XBUTTON1");
        RegisterEnum(VK_XBUTTON2, "VK_XBUTTON2");

        //Basic keyboard
        RegisterEnum(VK_BACK, "VK_BACK");
        RegisterEnum(VK_TAB, "VK_TAB");
        RegisterEnum(VK_RETURN, "VK_RETURN");
        RegisterEnum(VK_SHIFT, "VK_SHIFT");
        RegisterEnum(VK_CONTROL, "VK_CONTROL");
        RegisterEnum(VK_MENU, "VK_MENU");
        RegisterEnum(VK_PAUSE, "VK_PAUSE");
        RegisterEnum(VK_CAPITAL, "VK_CAPITAL");
        RegisterEnum(VK_ESCAPE, "VK_ESCAPE");
        RegisterEnum(VK_SPACE, "VK_SPACE");

        //Navigation
        RegisterEnum(VK_PRIOR, "VK_PRIOR");       //Page Up
        RegisterEnum(VK_NEXT, "VK_NEXT");         //Page Down
        RegisterEnum(VK_END, "VK_END");
        RegisterEnum(VK_HOME, "VK_HOME");
        RegisterEnum(VK_LEFT, "VK_LEFT");
        RegisterEnum(VK_UP, "VK_UP");
        RegisterEnum(VK_RIGHT, "VK_RIGHT");
        RegisterEnum(VK_DOWN, "VK_DOWN");
        RegisterEnum(VK_INSERT, "VK_INSERT");
        RegisterEnum(VK_DELETE, "VK_DELETE");

        //Numbers
        RegisterEnum('0', "VK_0");
        RegisterEnum('1', "VK_1");
        RegisterEnum('2', "VK_2");
        RegisterEnum('3', "VK_3");
        RegisterEnum('4', "VK_4");
        RegisterEnum('5', "VK_5");
        RegisterEnum('6', "VK_6");
        RegisterEnum('7', "VK_7");
        RegisterEnum('8', "VK_8");
        RegisterEnum('9', "VK_9");

        //Letters
        RegisterEnum('A', "VK_A");
        RegisterEnum('B', "VK_B");
        RegisterEnum('C', "VK_C");
        RegisterEnum('D', "VK_D");
        RegisterEnum('E', "VK_E");
        RegisterEnum('F', "VK_F");
        RegisterEnum('G', "VK_G");
        RegisterEnum('H', "VK_H");
        RegisterEnum('I', "VK_I");
        RegisterEnum('J', "VK_J");
        RegisterEnum('K', "VK_K");
        RegisterEnum('L', "VK_L");
        RegisterEnum('M', "VK_M");
        RegisterEnum('N', "VK_N");
        RegisterEnum('O', "VK_O");
        RegisterEnum('P', "VK_P");
        RegisterEnum('Q', "VK_Q");
        RegisterEnum('R', "VK_R");
        RegisterEnum('S', "VK_S");
        RegisterEnum('T', "VK_T");
        RegisterEnum('U', "VK_U");
        RegisterEnum('V', "VK_V");
        RegisterEnum('W', "VK_W");
        RegisterEnum('X', "VK_X");
        RegisterEnum('Y', "VK_Y");
        RegisterEnum('Z', "VK_Z");

        //Windows / application keys
        RegisterEnum(VK_LWIN, "VK_LWIN");
        RegisterEnum(VK_RWIN, "VK_RWIN");
        RegisterEnum(VK_APPS, "VK_APPS");
        RegisterEnum(VK_SLEEP, "VK_SLEEP");

        //Numpad
        RegisterEnum(VK_NUMPAD0, "VK_NUMPAD0");
        RegisterEnum(VK_NUMPAD1, "VK_NUMPAD1");
        RegisterEnum(VK_NUMPAD2, "VK_NUMPAD2");
        RegisterEnum(VK_NUMPAD3, "VK_NUMPAD3");
        RegisterEnum(VK_NUMPAD4, "VK_NUMPAD4");
        RegisterEnum(VK_NUMPAD5, "VK_NUMPAD5");
        RegisterEnum(VK_NUMPAD6, "VK_NUMPAD6");
        RegisterEnum(VK_NUMPAD7, "VK_NUMPAD7");
        RegisterEnum(VK_NUMPAD8, "VK_NUMPAD8");
        RegisterEnum(VK_NUMPAD9, "VK_NUMPAD9");

        RegisterEnum(VK_MULTIPLY, "VK_MULTIPLY");
        RegisterEnum(VK_ADD, "VK_ADD");
        RegisterEnum(VK_SUBTRACT, "VK_SUBTRACT");
        RegisterEnum(VK_DECIMAL, "VK_DECIMAL");
        RegisterEnum(VK_DIVIDE, "VK_DIVIDE");

        //Function keys
        RegisterEnum(VK_F1, "VK_F1");
        RegisterEnum(VK_F2, "VK_F2");
        RegisterEnum(VK_F3, "VK_F3");
        RegisterEnum(VK_F4, "VK_F4");
        RegisterEnum(VK_F5, "VK_F5");
        RegisterEnum(VK_F6, "VK_F6");
        RegisterEnum(VK_F7, "VK_F7");
        RegisterEnum(VK_F8, "VK_F8");
        RegisterEnum(VK_F9, "VK_F9");
        RegisterEnum(VK_F10, "VK_F10");
        RegisterEnum(VK_F11, "VK_F11");
        RegisterEnum(VK_F12, "VK_F12");
        RegisterEnum(VK_F13, "VK_F13");
        RegisterEnum(VK_F14, "VK_F14");
        RegisterEnum(VK_F15, "VK_F15");
        RegisterEnum(VK_F16, "VK_F16");
        RegisterEnum(VK_F17, "VK_F17");
        RegisterEnum(VK_F18, "VK_F18");
        RegisterEnum(VK_F19, "VK_F19");
        RegisterEnum(VK_F20, "VK_F20");
        RegisterEnum(VK_F21, "VK_F21");
        RegisterEnum(VK_F22, "VK_F22");
        RegisterEnum(VK_F23, "VK_F23");
        RegisterEnum(VK_F24, "VK_F24");

        //Lock keys
        RegisterEnum(VK_NUMLOCK, "VK_NUMLOCK");
        RegisterEnum(VK_SCROLL, "VK_SCROLL");

        //Left/right modifiers
        RegisterEnum(VK_LSHIFT, "VK_LSHIFT");
        RegisterEnum(VK_RSHIFT, "VK_RSHIFT");
        RegisterEnum(VK_LCONTROL, "VK_LCONTROL");
        RegisterEnum(VK_RCONTROL, "VK_RCONTROL");
        RegisterEnum(VK_LMENU, "VK_LMENU");
        RegisterEnum(VK_RMENU, "VK_RMENU");

        //OEM / punctuation keys
        RegisterEnum(VK_OEM_1, "VK_OEM_1");             // ; :
        RegisterEnum(VK_OEM_PLUS, "VK_OEM_PLUS");       // = +
        RegisterEnum(VK_OEM_COMMA, "VK_OEM_COMMA");     // , <
        RegisterEnum(VK_OEM_MINUS, "VK_OEM_MINUS");     // - _
        RegisterEnum(VK_OEM_PERIOD, "VK_OEM_PERIOD");   // . >
        RegisterEnum(VK_OEM_2, "VK_OEM_2");             // / ?
        RegisterEnum(VK_OEM_3, "VK_OEM_3");             // ` ~
        RegisterEnum(VK_OEM_4, "VK_OEM_4");             // [ {
        RegisterEnum(VK_OEM_5, "VK_OEM_5");             // \ |
        RegisterEnum(VK_OEM_6, "VK_OEM_6");             // ] }
        RegisterEnum(VK_OEM_7, "VK_OEM_7");             // ' "
        RegisterEnum(VK_OEM_8, "VK_OEM_8");
        RegisterEnum(VK_OEM_102, "VK_OEM_102");         // <> or \| depending on keyboard

        RegisterMemberFunction(Lua_SetBinding, "SetBinding");
        RegisterFunction(Lua_SendButton, "SendButton");
        RegisterFunction(Lua_Log, "Log");
    });

    Scope("VGuiPanel", [&]()
    {
        RegisterEnum(PANEL_ROOT, "ROOT");
        RegisterEnum(PANEL_GAMEUIDLL, "GAMEUIDLL");
        RegisterEnum(PANEL_CLIENTDLL, "CLIENTDLL");
        RegisterEnum(PANEL_TOOLS, "TOOLS");
        RegisterEnum(PANEL_INGAMESCREENS, "INGAMESCREENS");
        RegisterEnum(PANEL_GAMEDLL, "GAMEDLL");
        RegisterEnum(PANEL_CLIENTDLL_TOOLS, "CLIENTDLL_TOOLS");
        RegisterEnum(PANEL_SIZING, "SIZING");
    });

    RegisterStruct<LuaPanel>("LuaPanel", [](auto& type) { type.NoConstructor(); });
    RegisterStruct<LuaAction>("ActionHandle", [](auto& type) { type.NoConstructor(); });
    RegisterStruct<StringPair>("StringPair", [](auto& type)
    {
        type.Constructor<>();
        type.Constructor <std::string, std::string> ();

        type.Member("m_Str1", &StringPair::m_Str1);
        type.Member("m_Str2", &StringPair::m_Str2);
    });

#pragma endregion

    ReloadFiles();

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
    std::string path = m_WorkingDir + filename;

    if (luaL_dofile(m_State, path.c_str()) != LUA_OK)
    {
        const char* error = lua_tostring(m_State, -1);
        Game::logMsg(LOGTYPE_ERROR, "LuaManager: Failed to load '%s': %s", filename, error ? error : "Unknown error");

        lua_pop(m_State, 1);
        return false;
    }

    return true;
}

void LuaManager::ReloadFiles()
{
    if (!m_State) return;

    std::filesystem::path scriptsPath(m_WorkingDir);
    if (!std::filesystem::exists(scriptsPath))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(scriptsPath))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".lua") continue;

        ExecuteFile(entry.path().string().c_str());
    }
}


int LuaManager::Lua_Log(lua_State* state)
{
    const char* message = luaL_checkstring(state, 1);
    Game::logMsg(LOGTYPE_LUA, "%s", message);
    return 0;
}

void LuaManager::Lua_PreUpdate()
{
    CallLuaFunction("PreUpdate");
}

void LuaManager::Lua_TextureMapping()
{
    CallLuaFunction("TextureMapping");
}

int LuaManager::Lua_GetPanel(lua_State* state)
{
    VGuiPanel_t type = static_cast<VGuiPanel_t>(luaL_checkinteger(state, 1));
    VPANEL panel = g_Game->m_EnginePanel->GetPanel(type);

    if (!panel)
    {
        lua_pushnil(state);
        return 1;
    }

    LuaPanel luaPanel;
    luaPanel.panel = panel;

    LuaPush(state, luaPanel);
    return 1;
}

int LuaManager::Lua_FindParentOf(lua_State* state)
{
    LuaPanel* panel = LuaCheckStruct<LuaPanel>(state, 1);

    const char* target = luaL_checkstring(state, 2);

    VPANEL result = g_Game->m_VR->FindParentOf(panel->panel, target);

    if (!result)
    {
        lua_pushnil(state);
        return 1;
    }

    LuaPanel resultPanel;
    resultPanel.panel = result;

    LuaPush(state, resultPanel);
    return 1;
}

int LuaManager::Lua_SetBinding(lua_State* state)
{
    LuaManager* manager = static_cast<LuaManager*>(lua_touserdata(state, lua_upvalueindex(1)));

    if (!manager)
        return luaL_error(state, "VR.SetBinding: LuaManager instance is invalid");

    const char* actionName = luaL_checkstring(state, 1);

    VRBindingType bindingType = static_cast<VRBindingType>(luaL_checkinteger(state, 2));
    StringPair* pair = LuaGetStruct<StringPair>(state, 3);
    VRBindingMode mode = static_cast<VRBindingMode>(luaL_checkinteger(state, 4));

    luaL_checktype(state, 5, LUA_TFUNCTION);
    lua_pushvalue(state, 5);
    int callbackRef = luaL_ref(state, LUA_REGISTRYINDEX);

    std::function<void(vr::VRActionHandle_t)> callback = [manager, callbackRef](vr::VRActionHandle_t handle)
    {
        lua_rawgeti(manager->m_State, LUA_REGISTRYINDEX, callbackRef);
        LuaPush(manager->m_State, LuaAction{ handle });

        if (lua_pcall(manager->m_State, 1, 0, 0) != LUA_OK)
        {
            const char* error = lua_tostring(manager->m_State, -1);
            manager->m_Game->logMsg(LOGTYPE_ERROR, "LuaManager: SetBinding callback error %s", error ? error : "unknown error");
            lua_pop(manager->m_State, 1);
        }
    };

    manager->m_Game->m_VR->SetBinding(actionName, bindingType, *pair, mode, callback);
    return 0;
}

int LuaManager::Lua_SetTurnBinding(lua_State * state)
{
    LuaManager* manager = static_cast<LuaManager*>(lua_touserdata(state, lua_upvalueindex(1)));

    if (!manager)
        return luaL_error(state, "VR.SetTurnBinding: LuaManager instance is invalid");

    const char* actionName = luaL_checkstring(state, 1);
    manager->m_Game->m_VR->SetBinding(actionName, VRBindingType_Analog, {}, VRBindingMode_Button, [](vr::VRActionHandle_t handle)
    {
        vr::InputAnalogActionData_t analogActionData;
        if (g_Game->m_VR->GetAnalogActionData(handle, analogActionData))
        {
            if (g_Game->m_VR->m_Config.m_SnapTurning)
            {
                if (!g_Game->m_VR->m_PressedTurn)
                {
                    if (analogActionData.x > 0.5f)
                    {
                        g_Game->m_VR->m_RotationOffset.y -= g_Game->m_VR->m_Config.m_SnapTurnAngle;
                        g_Game->m_VR->m_PressedTurn = true;
                    }
                    else if (analogActionData.x < -0.5f)
                    {
                        g_Game->m_VR->m_RotationOffset.y += g_Game->m_VR->m_Config.m_SnapTurnAngle;
                        g_Game->m_VR->m_PressedTurn = true;
                    }
                }

                if (fabsf(analogActionData.x) < 0.3f) g_Game->m_VR->m_PressedTurn = false;
            }
            else
            {
                auto currentTime = std::chrono::steady_clock::now();
                float deltaTime = std::chrono::duration<float, std::milli>(currentTime - g_Game->m_VR->m_PrevFrameTime).count();
                g_Game->m_VR->m_PrevFrameTime = currentTime;
                constexpr float deadzone = 0.2f;
                float x = analogActionData.x;
                float magnitude = fabsf(x);

                if (magnitude > deadzone)
                {
                    float normalized = (magnitude - deadzone) / (1.0f - deadzone);
                    float direction = (x > 0.0f) ? -1.0f : 1.0f;
                    g_Game->m_VR->m_RotationOffset.y += direction * g_Game->m_VR->m_Config.m_TurnSpeed * deltaTime * normalized;
                }
            }

            g_Game->m_VR->m_RotationOffset.y -= 360.0f * floorf(g_Game->m_VR->m_RotationOffset.y / 360.0f);
        }
    });

    return 0;
}

int LuaManager::Lua_SetWalkBinding(lua_State* state)
{
    LuaManager* manager = static_cast<LuaManager*>(lua_touserdata(state, lua_upvalueindex(1)));

    if (!manager)
        return luaL_error(state, "VR.SetWalkBinding: LuaManager instance is invalid");

    const char* actionName = luaL_checkstring(state, 1);

    g_Game->m_VR->m_Input->GetActionHandle(actionName, &g_Game->m_VR->m_ActionWalk);
    return 0;
}

void LuaManager::Lua_CreateControllerBindings()
{
    CallLuaFunction("CreateControllerBindings");
}

int LuaManager::Lua_SendButton(lua_State* state)
{
    int key = luaL_checkinteger(state, 1);
    g_Game->m_VR->SendButton(key);
    return 0;
}

int LuaManager::Lua_IsInGame(lua_State* state)
{
    lua_pushboolean(state, g_Game->m_EngineClient->IsInGame());
    return 1;
}