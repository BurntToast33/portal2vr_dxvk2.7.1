#pragma once
#include "game.h"
#include "vr.h"
#include <algorithm>

struct ParalaxMapInfo
{
	//Image
	std::string m_ImagePath;
	int m_Width;
	int m_Height;
	int m_MipLevels;
	D3DFORMAT m_Format;

	float m_CaptureHeight;
	Vector m_BoxMin;
	Vector m_BoxMax;

	bool m_Error = false; //This will prvent maps that failed parsing from loading
};

enum PCCM_GENERATION_STATE {
	PCCM_GENERATION_STATE_ERROR = -1,
	PCCM_GENERATION_STATE_DISABLED,
	PCCM_GENERATION_STATE_ENABLED,
	PCCM_GENERATION_STATE_CMTSET //CubeMapTexture
};

inline void from_json(const nlohmann::json& j, ParalaxMapInfo& info)
{
	try
	{
		j.at("imagePath").get_to(info.m_ImagePath);
		j.at("captureHeight").get_to(info.m_CaptureHeight);
		j.at("boxMin").get_to(info.m_BoxMin);
		j.at("boxMax").get_to(info.m_BoxMax);
	}
	catch (const std::exception& e)
	{
		throw std::runtime_error(
			std::string("Failed parsing ParalaxMapInfo: ") + e.what()
		);
	}
}

class PCCM
{
public:
	Game* m_Game = nullptr;

	PCCM_GENERATION_STATE m_GeneratingCubeMapState = PCCM_GENERATION_STATE_DISABLED;
	UINT16 m_GenerationResolution = 0;
	SharedTextureHolder m_GenerationTexture;

	std::unordered_map<std::string, ParalaxMapInfo> m_Mappings;
	dxvk::D3D9DeviceEx* m_DxDevice = nullptr;

	IDirect3DCubeTexture9* m_CubeMap = nullptr;

	UINT GetMipSize(UINT width, UINT height, D3DFORMAT format)
	{
		switch (format)
		{
			case D3DFMT_DXT1:
			{
				UINT blocksX = std::max(1u, (width + 3) / 4);
				UINT blocksY = std::max(1u, (height + 3) / 4);
				return blocksX * blocksY * 8;
			}
			case D3DFMT_DXT3:
			case D3DFMT_DXT5:
			{
				UINT blocksX = std::max(1u, (width + 3) / 4);
				UINT blocksY = std::max(1u, (height + 3) / 4);
				return blocksX * blocksY * 16;
			}
			case D3DFMT_A8R8G8B8:
			case D3DFMT_X8R8G8B8:
			{
				return width * height * 4;
			}
		}

		return 0;
	}

	PCCM(Game* game);
	PCCM(Game* game, std::unordered_map<std::string, ParalaxMapInfo>& mappings);
	~PCCM();

	//Setup
	void UploadPCCMMap(std::unordered_map<std::string, ParalaxMapInfo>& mappings);
	void LoadCubeMap(const char* mapName);
	void ParseDDSCubemap(ParalaxMapInfo& info);
	int UploadDDSCubemap(ParalaxMapInfo& info);

	//Rendering
	bool RenderPCCM(SharedTextureHolder* leftEye, SharedTextureHolder* rightEye);

	//Building cube map
	void SetupCubemapView(CViewSetup& view, int face);
	void WriteImageToCubeMap(int face);
	void SaveImageToDSS(CViewSetup& view);
	std::string GenerateCubeFileName(UINT resolution);
	void SavePCCMJson(const std::filesystem::path& jsonPath, CViewSetup& view);
};