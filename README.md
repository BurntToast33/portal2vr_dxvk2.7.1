<div align="center">
  <p>
    <a align="center" href="https://ultralytics.com/yolov5" target="_blank">
      <img width="auto" src="https://raw.githubusercontent.com/Gistix/portal2vr/main/imgs/logo.png"></a>
  </p>
</div>

# ![Portal 2 icon](imgs/icon.jpg "Portal 2 icon") Portal 2 VR
### ~~Use this mod at your own risk of getting VAC banned. Use the -insecure launch option to help protect yourself.~~
### Apparently Portal 2 doesn't have VAC, but just to be safe you should still run the game with the `insecure` flag.
This game contains flashing lights and fast motion sequences.

## Portal 2 VR Mod First 20 Minutes (Youtube Video)
[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/nQZ601kEDFI/0.jpg)](https://www.youtube.com/watch?v=nQZ601kEDFI)

## Things that work
* 3D menus
* Singleplayer
* Hud for vr player and spectators
* 6DoF VR view
* Motion controls for portal gun and grabbable objects
* Workshop content

## Things that need fixing
* Use the game's own haptic feedback
* Roomscale needs to be reimplemented
* CPU is underutilized

## How to use
1. Download [Portal2VR.zip](https://github.com/BurntToast33/portal2vr/releases) and extract the files to your Portal 2 directory ```steamapps\common\Portal 2```
2. Connect your headset, then launch Portal 2 with these launch options:
   
   ``` -vr -insecure -window -novid -width 1280 -height 720 ```

   The (-vr) flag can switch between vr mode and desktop mode.
   The height and width of the window effects ui resolution.

4. At the menu, feel free to change [these video settings](https://i.imgur.com/yYQMXs6.jpg).
5. Load into a chapter. 
6. To recenter the camera height, press down on the left stick.

# On Linux
- Add `WINEDLLOVERRIDES="d3d9=n,b" %command%` to the start of the launch args. (tested on cachyos)
- If steamvr crashes when starting up, put the headset down and let the displays turn off, then start the game. 

## How to change vr settings
Most VR settings have been moved into in game menus and a button has been added to open the controller rebinding ui.

~~Go to ```steamapps\common\Portal 2\VR```~~
~~- Open config.txt for vr settings,~~
~~- Open backgrounds.txt for what background map will be loaded based on where you are in your save file.~~
~~- To rebind controlls go to ```SteamVRActionManifest``` and open the corresponding controller you have and mannualy re-map it.~~

## Troubleshooting
If you have no audio:
* Go to ```steamapps\common\Portal 2\portal2_dlc3``` and execute ```UpdateSoundCache.cmd```
  
If the game isn't loading in VR:
* Try opening SteamVR before the game
* Disable SteamVR theater in [Steam settings](https://external-preview.redd.it/1WdLExouo_YKhTGT6C5GGrOjeWO7qNdIdDRvIRBhw-0.png?auto=webp&s=0d4447a9d954e1ec15b2c010cf50eeabd51f4197)

If the game is stuttering, try: 
* Steam Settings -> Shader Pre-Caching -> Allow background processing of Vulkan shaders

If the game is crashing, try:
* Lowering video settings
* Disabling all add-ons then verifying integrity of game files
* Re-installing the game

## Build instructions
1. ``` git clone --recurse-submodules https://github.com/BurntToast33/portal2vr_dxvk2.7.1.git ```
2. Open Portal2VR_2.7.1.sln
3. Set to x86 Release
4. Build -> Build Solution

Note: After building, it will attempt to copy the new d3d9.dll to your Portal 2/bin directory.

## Based on
* [l4d2vr](https://github.com/sd805/l4d2vr)
* [original portal2vr by Gistix](https://github.com/Gistix/portal2vr)
  
## Utilizes code from
* [VirtualFortress2](https://github.com/PinkMilkProductions/VirtualFortress2)
* [gmcl_openvr](https://github.com/Planimeter/gmcl_openvr/)
* [dxvk](https://github.com/TheIronWolfModding/dxvk/tree/vr-dx9-rel)
* [source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013/)

## Support Gistix
<a href="https://www.paypal.com/donate/?business=YL7TGWKPCC9H8&no_recurring=0&currency_code=USD"><img src="https://pics.paypal.com/00/s/MDAwNDljNmUtZWZiZS00ZTI1LWFiMTMtZTdhZmQ5NmU5ZDUx/file.PNG" alt="Donate Button" style="width:auto;height:100px;"></a>

