# Lua API Documentation

This document contains the complete API exposed to Lua by the VR mod.

Lua scripts are located in:

`<Game Directory>/VR/Scripts/<gameType>/`

---

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

# Functions

`game.log()`

Logged messages are printed to the VR console and written to the log file.
```lua
game.log("String message")
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