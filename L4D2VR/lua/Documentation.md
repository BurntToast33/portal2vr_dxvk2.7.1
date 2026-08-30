# Lua API Documentation

This document contains the complete API exposed to Lua by the VR mod.

Lua scripts are located in: `<Game Directory>/VR/Scripts/<gameType>/`

# Enums
`VGuiPanel`

Defines the available VGUI tree roots that can be used when locating and mapping VGUI elements.

```lua
VGuiPanel.ROOT
VGuiPanel.GAMEUIDLL
VGuiPanel.CLIENTDLL
VGuiPanel.TOOLS
VGuiPanel.INGAMESCREENS
VGuiPanel.GAMEDLL
VGuiPanel.CLIENTDLL_TOOLS
VGuiPanel.SIZING
```

---
`VRBINDINGTYPE`

Defines how controller binds are processed.
- `VRBINDINGTYPE_NONE` No input processing is performed for the binding.
- `VRBINDINGTYPE_INPUT` Processes input when no menu is active. Supports digital button press/release events, commands, callbacks, and optional hold/toggle behavior.
- `VRBINDINGTYPE_MENU` Processes input when a menu is active. Supports digital button press/release events, commands, callbacks, and optional hold/toggle behavior.
- `VRBINDINGTYPE_ANALOG` Processes analog input when no menu is active, such as joysticks or trackpads. The callback is invoked every frame and is responsible for handling the input. Press/release commands and hold behavior are not supported.

```lua
VR.VRBINDINGTYPE_NONE
VR.VRBINDINGTYPE_INPUT
VR.VRBINDINGTYPE_MENU
VR.VRBINDINGTYPE_ANALOG
```

---
`VRBINDINGMODE`

Defines how a controller bind is interpreted.
- `VRBINDINGMODE_BUTTON` Triggers once when the button is pressed.
- `VRBINDINGMODE_TOGGLE` Toggles between on and off each time the button is pressed.
- `VRBINDINGMODE_HOLD` Triggers when the button is pressed and again when it is released.
- `VRBINDINGMODE_REPEAT` Triggers repeatedly while the button is held.

```lua
VR.VRBINDINGMODE_BUTTON
VR.VRBINDINGMODE_TOGGLE
VR.VRBINDINGMODE_HOLD
VR.VRBINDINGMODE_REPEAT
```

---
`VK_`

Windows keys

```lua
VK_LBUTTON
VK_RBUTTON
VK_MBUTTON
VK_XBUTTON1
VK_XBUTTON2

VK_BACK
VK_TAB
VK_RETURN
VK_SHIFT
VK_CONTROL
VK_MENU
VK_PAUSE
VK_CAPITAL
VK_ESCAPE
VK_SPACE

VK_PRIOR
VK_NEXT
VK_END
VK_HOME
VK_LEFT
VK_UP
VK_RIGHT
VK_DOWN
VK_INSERT
VK_DELETE

VK_0
VK_1
VK_2
VK_3
VK_4
VK_5
VK_6
VK_7
VK_8
VK_9

VK_A
VK_B
VK_C
VK_D
VK_E
VK_F
VK_G
VK_H
VK_I
VK_J
VK_K
VK_L
VK_M
VK_N
VK_O
VK_P
VK_Q
VK_R
VK_S
VK_T
VK_U
VK_V
VK_W
VK_X
VK_Y
VK_Z

VK_LWIN
VK_RWIN
VK_APPS
VK_SLEEP

VK_NUMPAD0
VK_NUMPAD1
VK_NUMPAD2
VK_NUMPAD3
VK_NUMPAD4
VK_NUMPAD5
VK_NUMPAD6
VK_NUMPAD7
VK_NUMPAD8
VK_NUMPAD9

VK_MULTIPLY
VK_ADD
VK_SUBTRACT
VK_DECIMAL
VK_DIVIDE

VK_F1
VK_F2
VK_F3
VK_F4
VK_F5
VK_F6
VK_F7
VK_F8
VK_F9
VK_F10
VK_F11
VK_F12
VK_F13
VK_F14
VK_F15
VK_F16
VK_F17
VK_F18
VK_F19
VK_F20
VK_F21
VK_F22
VK_F23
VK_F24

VK_NUMLOCK
VK_SCROLL

VK_LSHIFT
VK_RSHIFT
VK_LCONTROL
VK_RCONTROL
VK_LMENU
VK_RMENU

VK_OEM_1
VK_OEM_PLUS
VK_OEM_COMMA
VK_OEM_MINUS
VK_OEM_PERIOD
VK_OEM_2
VK_OEM_3
VK_OEM_4
VK_OEM_5
VK_OEM_6
VK_OEM_7
VK_OEM_8
VK_OEM_102
```

# Data structures
`StringPair`

A pair of 2 strings in a single variable.
- `m_Str1` String 1
- `m_Str2` String2

```lua
local commands = StringPair()
commands.m_Str1_ = "+jump"
commands.m_Str2_ = "-jump"
```

---

`ActionHandle`
A handle on the action state of the current action being processed.
- Used in SetBinding

# Functions
`log(string)`

Logged messages are printed to the VR console and written to the log file.
```lua
VR.log("String message")
```

---
`GetPanel(VGuiPanel)`

Gets the specified root panel from the game.
returns a LuaPanel.
```lua
Game.GetPanel(VGuiPanel.ROOT);
```

---
`FindParentOf(LuaPanel, string)`

Tries to find the first parent panel with a matching name. 
if no parent is found returns null.
returns a LuaPanel.
```lua
Game.FindParentOf(startPanel, "HudWeapon")
```

---
`SetBinding(string, VRBINDINGTYPE, StringPair, VRBINDINGMODE, function(handle))`
- actionName — The name of the VR action to bind.
- bindingType — Determines how the binding is processed. See VRBINDINGTYPE.
- commands — A StringPair containing the command to execute on press and release.
- mode — Determines how the binding responds to input. See VRBINDINGMODE.
- callback — Optional function called when the binding is activated. Receives the VR action handle as handle.

```lua
VR.SetBinding( "/actions/main/in/Jump", VRBINDINGTYPE_INPUT, commands, VRBINDINGMODE_BUTTON,
function(handle)
    Game.log("Jump binding activated")
end)
```

---
`SetTurnBinding(string)`
Takes action and binds it to turning code.

```lua
VR.SetTurnBinding("/actions/main/in/Turn")
```

---
`SetWalkBinding(string)`
Takes action and binds it to movement code.

```lua
VR.SetWalkBinding("/actions/main/in/Walk")
```

---
`SendButton(VK_)`
Takes `VK_` and processes the key command to be sent.

```lua
VR.SendButton(VK_RIGHT)
```

---
`IsInGame()`
Returns boolean if the player is in a level.

```lua
if Game.IsInGame() then

end
```

# Callback Functions
`function PreUpdate()`

Callback function called just before submitting frames.
```lua
function PreUpdate()
    -- Code executed before frame submission
end
```

---
`function TextureMapping()`

Callback function called after every map load to configure which VGUI elements are captured to which textures.
```lua
function TextureMapping()
    -- Configure texture mappings
end
```

---
`function CreateControllerBindings()`

Callback function called during vr intilization to configure what controller bindings do.
```lua
function CreateControllerBinding()
    -- Configure controller bindings
end
```