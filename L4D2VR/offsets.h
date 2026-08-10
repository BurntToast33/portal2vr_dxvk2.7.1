#pragma once
#include "sigscanner.h"
#include "game.h"


struct Offset
{
    std::string moduleName;
    std::string hookName;
    uintptr_t offset = 0;
    uintptr_t address = 0;
    std::string signature;
    int sigOffset = 0;

    Offset(std::string hookName = "", std::string moduleName = "", int currentOffset = 0, 
        std::string signature = "", int sigOffset = 0)
    {
        if (hookName.empty() && moduleName.empty() && currentOffset == 0 && signature.empty() && sigOffset == 0)
            return;

        this->hookName = hookName;
        this->moduleName = moduleName;
        this->offset = currentOffset;
        this->signature = signature;
        this->sigOffset = sigOffset;

        int newOffset = SigScanner::VerifyOffset(hookName, moduleName, currentOffset, signature, sigOffset);
        if (newOffset > 0)
            this->offset = newOffset;

        if (newOffset == -1)
            return;

        HMODULE hMod = GetModuleHandle(moduleName.c_str());
        if (!hMod)
        {
            Game::errorMsg((hookName + ": Module not found: " + moduleName).c_str());
            return;
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(hMod);
        this->address = base + this->offset;
    }
};

class Offsets
{
public:
    Offset CBaseEntity_entindex = { "CBaseEntity_entindex", DLL_SERVER, 0x3A050, "8B 41 1C 85 C0 75 01 C3 8B 0D ? ? ? ? 2B 41 58 C1 F8 04 C3 CC" };
    Offset PlayerPortalled = { "PlayerPortalled", DLL_CLIENT, 0x27CB70, "55 8B EC 83 EC 78 53 56 8B D9 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 57 33 FF 57 FF D2" };
    Offset GetName; //KeyValues
    Offset GetString = { "GetString", DLL_CLIENT, 0x62FAD0, "55 8B EC 81 EC 40 02 00 00 53 56 57 8B 7D 08" }; //KeyValues
    Offset SetString = { "SetString", DLL_CLIENT, 0x62E9F0, "55 8B EC 8B 45 08 6A 01 50 E8 32 FB FF FF" }; //KeyValues
    Offset GetFloat = { "GetFloat", DLL_CLIENT, 0x62E810, "55 8B EC 8b 45 08 83 EC 08 6A 00 50 E8 0F FD FF FF" }; //KeyValues

    //Movement
    Offset ProcessUsercmds = { "ProcessUsercmds", DLL_SERVER, 0x170DA0, "55 8B EC B8 ? ? ? ? E8 ? ? ? ? 0F 57 C0 53 56 57 B9 ? ? ? ? 8D 85 ? ? ? ? 33 DB" };
    Offset ReadUserCmd = { "ReadUserCmd", DLL_SERVER, 0x205B4E, "55 8B EC 53 8B 5D 10 56 57 8B 7D 0C 53" };
    Offset WriteUsercmd = { "WriteUsercmd", DLL_CLIENT, 0x1C26E0, "55 8B EC A1 ? ? ? ? 83 78 30 00 53 8B 5D 0C 56 57" };
    Offset CreateMove = { "CreateMove", DLL_CLIENT, 0x27A570, "55 8B EC A1 ? ? ? ? 83 EC 0C 83 78 30 00 56 8B 75 0C 57 8B F9 74 43" };

    //Weapon
    Offset TraceFirePortalServer = { "TraceFirePortalServer", DLL_SERVER, 0x400DE0, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A" };
    Offset CWeaponPortalgun_FirePortal = { "CWeaponPortalgun_FirePortal", DLL_SERVER, 0x401400, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 89 7D EC E8 ? ? ? ?" };
    Offset Weapon_ShootPosition = { "Weapon_ShootPosition", DLL_SERVER, 0x368270, "55 8B EC 8B 01 8B 90 ? ? ? ? 56 8B 75 08 56 FF D2 8B C6 5E 5D C2 04 00" };

    //Rendering
    Offset RenderView = { "RenderView", DLL_CLIENT, 0x1F2620, "55 8B EC 83 EC 2C 53 56 8B F1 6A 00 8D 8E ? ? ? ? E8 ? ? ? ?" };
    Offset CalcViewModelView = { "CalcViewModelView", DLL_CLIENT, 0x27D8F0, "55 8B EC 83 EC 34 53 8B D9 80 BB" };
    Offset SetDrawOnlyForSplitScreenUser = { "SetDrawOnlyForSplitScreenUser", DLL_CLIENT, 0x17B9E0, "55 8B EC 8B 45 08 53 8B D9 3B 83 ? ? ? ? 74 55" };
    Offset ComputeShadowDepthTextures;
    Offset UnlockAllShadowDepthTextures = { "UnlockAllShadowDepthTextures", DLL_CLIENT, 0xEF110, "33 C0 39 81 6C 01 00 00 7E 19 8D 9B 00 00 00 00" };
    Offset FormatViewModelAttachment = { "FormatViewModelAttachment", DLL_CLIENT, 0x951E0, "55 8B EC 8b 45 08 83 EC 28 53 56 57 33 FF 85 C0" };
    Offset CWLC_Flush = { "CWLC_Flush", DLL_CLIENT, 0x1EC3E0, "55 8B EC 83 EC 08 53 57 8B F9 89 7D FC E8 CE F6 ? ?" }; //CWorldListCache

    //In game UI
    Offset PaintTraverse;
    Offset PrepareCredits = { "PrepareCredits", DLL_CLIENT, 0x292D20, "55 8B EC 56 57 8B F9 E8 04 F6 ? ? 6A 24" };
    Offset PostActionSignal = { "PostActionSignal", DLL_CLIENT, 0x646F70, "55 8B EC 83 EC 08 53 56 8B F1 F6 86 B4 00 00 00 04" }; //Panel
    Offset LoadControlSettings; //BuildGroup
    Offset ApplySettings = { "ApplySettings", DLL_CLIENT, 0x6508A0, "55 8B EC 83 EC 4C 56 ? F1 F6 86 B4 00 00 00 01" }; //Panel
    Offset UpdateProgressBar = { "UpdateProgressBar", DLL_CLIENT, 0x36FD50, "55 8B EC 83 EC 08 56 8B F1 80 BE B4 01 00 00 00" }; //SlideControl
   
    //Assist Laser
    Offset PrecacheParticleSystem;
    Offset SetControlPoint = { "SetControlPoint", DLL_CLIENT, 0x17C230, "55 8B EC 53 56 8B 75 0C 57 8B F9 BB ? ? ? ? 84 9F ? ? ? ?"};
    Offset StopEmission = { "StopEmission", DLL_CLIENT, 0x17BBA0, "55 8B EC 53 8B 5D 08 57 8B F9 F6 87 ? ? ? ? ? 74 7F" };
    Offset CreateParticle = { "CreateParticle", DLL_CLIENT, 0x1737D0, "55 8B EC 8B 55 10 83 EC 0C 53 56 8B F1 8B 4D 08" };
    Offset PushAllowBoneAccess = { "PushAllowBoneAccess", DLL_CLIENT, 0x64970, "55 8B EC 83 EC 08 FF 15 A4 A2 ? ? 84 C0 74 3F" }; //C_BaseAnimating
    Offset PopBoneAccess = { "PopBoneAccess", DLL_CLIENT, 0x60430, "FF 15 A4 A2 ? ? 84 C0 74 4b A1 28 62 ? ? 8D 50 FF" }; //C_BaseAnimating
    Offset GetViewModel = { "GetViewModel", DLL_CLIENT, 0x91370, "55 8B EC 51 8B 45 08 53 57 8B F9 8B 8C 87 94 16 00 00" };

    //VR Eyes
    Offset UTIL_Portal_FirstAlongRay = { "UTIL_Portal_FirstAlongRay", DLL_SERVER, 0x377A30, "55 8B EC 8B 0D ? ? ? ? 85 C9 74 19 A1 ? ? ? ?"};
    Offset UTIL_IntersectRayWithPortal = { "UTIL_IntersectRayWithPortal", DLL_SERVER, 0x376F60, "55 8B EC 83 EC 48 56 8B 75 0C 85 F6 0F 84 ? ? ? ?"};
    Offset UTIL_Portal_AngleTransform = { "UTIL_Portal_AngleTransform", DLL_SERVER, 0x3764D0, "55 8B EC 8B 45 08 8B 4D 0C 83 EC 0C 50 51 8D 55 F4"};

    //Grabbles
    Offset ComputeError = { "ComputeError", DLL_SERVER, 0x3C82F0, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 8B F1 8B 86 ? ? ? ? 57 83 F8 FF 74 2A"};
    Offset UpdateObject = { "UpdateObject", DLL_SERVER, 0x3CA1C0, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 8B 87 ? ? ? ? 89 BD"};
    Offset RotateObject = { "RotateObject", DLL_SERVER, 0x3C7A40, "55 8B EC 0F 57 C0 F3 0F 10 4D ? 81 EC ? ? ? ? 0F 2E C8 9F 57 8B F9 F6 C4 44 7A 12"};
    Offset EyeAngles = { "EyeAngles", DLL_SERVER, 0x104590, "55 8B EC 8B 81 ? ? ? ? 83 EC 60 56 57 8B 3D ? ? ? ? 83 F8 FF 74 1D"};

    //For Portal gun VFX (do we really need all three??)
    Offset GetFOV = { "GetFOV", DLL_CLIENT, 0x2773C0, "55 8B EC 51 56 8B F1 E8 ? ? ? ? D9 5D FC 8B 06 8B 90 ? ? ? ? 8B CE FF D2" };
    Offset GetDefaultFOV = { "GetDefaultFOV", DLL_CLIENT, 0x279130, "A1 ? ? ? ? F3 0F 2C 40 ? C3" };
    Offset GetViewModelFOV = { "GetViewModelFOV", DLL_CLIENT, 0x0E7D20, "A1 ? ? ? ? D9 40 2C C3" };

    //Multiplayer
    Offset GetOwner = { "GetOwner", DLL_SERVER, 0xD7C00, "8B 81 ? ? ? ? 83 F8 FF 74 23 8B 15 ? ? ? ?"};

    //Map related
    Offset LevelInit; //CServerGameDLL


    Offsets(GAMETYPE GameType) :
        GetName(),
        ComputeShadowDepthTextures(),
        PrecacheParticleSystem(),
        LevelInit(),
        PaintTraverse(),
        LoadControlSettings()
    {
        switch (GameType)
        {
            case GAMETYPE_PORTAL2: 
            {
                GetName = { "GetName", DLL_CLIENT, 0x62D3E0, "56 8B F1 85 F6 74 1F FF 15 90 A3 ? ?" };
                ComputeShadowDepthTextures = { "ComputeShadowDepthTextures", DLL_CLIENT, 0xF1C10, "55 8B EC 81 EC 78 09 00 00 A1 9C EE ? ?" };
                PrecacheParticleSystem = { "PrecacheParticleSystem", DLL_CLIENT, 0xD1530, "55 8B EC 8B 0D AC A2 ? ? 8B 01 8B 50 20 56 57" };
                LevelInit = { "LevelInit", DLL_SERVER, 0x1720B0, "55 8B EC 53 56 57 8B F9 E8 03 62 FD FF 8B 5D 08 53" };
                PaintTraverse = { "PaintTraverse", DLL_VGUI2, 0x197D0, "55 8B EC 8B 01 8B 55 08 8B 80 04 01" };
                LoadControlSettings = { "LoadControlSettings", DLL_CLIENT, 0x6A62A0, "55 8B EC 8B 45 0C 83 EC 08 53 56 57 8B 7D 08" };
                break;
            }
            case GAMETYPE_PORTAl_RELOADED:
            {
                GetName = { "GetName", DLL_CLIENT, 0x628160, "56 8B F1 85 f6 74 1F FF 15 90 43 ? ?" };
                ComputeShadowDepthTextures = { "ComputeShadowDepthTextures", DLL_CLIENT, 0xF0E40, "55 8B EC 81 EC 78 09 00 00 A1 84 AB ? ?" };
                PrecacheParticleSystem = { "PrecacheParticleSystem", DLL_CLIENT, 0xD0890, "55 8B EC 8B 0D 94 5F ? ? 8B 01 8B 50 20 56 57" };
                LevelInit = { "LevelInit", DLL_SERVER, 0x16FD10, "55 8B EC 53 56 57 8B F9 E8 D3 68 FD FF 8b 5d 08 53" };
                PaintTraverse = { "PaintTraverse", DLL_VGUI2, 0x196A0, "55 8B EC 8B 01 8B 55 08 8B 80 04 01" };
                LoadControlSettings = { "LoadControlSettings", DLL_CLIENT, 0x6A0F50, "55 8B EC 8B 45 0C 83 EC 08 53 56 57 8B 7D 08" };
                break;
            }
        }
    }
};