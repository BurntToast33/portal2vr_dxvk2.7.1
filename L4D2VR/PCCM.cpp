#include "PCCM.h"
#include "sdk.h"
#include "DirectXTex.h"

#define DDPF_FOURCC       0x00000004
#define DDPF_RGB          0x00000040


#pragma pack(push,1)
struct DDS_PIXELFORMAT
{
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDS_HEADER
{
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    uint32_t dwPitchOrLinearSize;
    uint32_t dwDepth;
    uint32_t dwMipMapCount;
    uint32_t dwReserved1[11];

    DDS_PIXELFORMAT ddspf;

    uint32_t dwCaps;
    uint32_t dwCaps2;
    uint32_t dwCaps3;
    uint32_t dwCaps4;
    uint32_t dwReserved2;
};
#pragma pack(pop)

void BuildPCCM(const CCommand& args)
{
    if (args.ArgC() != 2)
    {
        g_Game->logMsg(LOGTYPE_INFO, "Build_PCCM command error, usage is: Build_PCCM <number>");
        return;
    }

    int value = atoi(args.Arg(1));
    if (value < 1)
    {
        g_Game->logMsg(LOGTYPE_INFO, "Build_PCCM command error: Minimum value is 1");
        return;
    }

    g_Game->m_PCCM->m_GenerationResolution = value;
    g_Game->m_PCCM->m_GeneratingCubeMapState = PCCM_GENERATION_STATE_ENABLED;
    g_Game->logMsg(LOGTYPE_DEBUG, "Building PCCM");
}



PCCM::PCCM(Game* game) : m_Game(game), m_DxDevice(game->m_DxDevice) 
{
    if (m_Game->m_VRDevMode) 
    {
        /*m_Game->m_MaterialSystem->isGameRunning = false;
        m_Game->m_MaterialSystem->BeginRenderTargetAllocation();
        m_Game->m_MaterialSystem->isGameRunning = true;

        CreateRT(&m_GenerationTexture, "PCCMGenTex", m_GenerationResolution, m_GenerationResolution, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat());
        m_Game->m_MaterialSystem->EndRenderTargetAllocation();*/
    }

    static ConCommand con_BuildPCCM("Build_PCCM", BuildPCCM, "Builds a cube map on the players camera posistion at the specified resolution, Saves to root/VR/PCCM", FCVAR_CHEAT);
    m_Game->logMsg(LOGTYPE_DEBUG, "PCCM Initilized");
}

PCCM::PCCM(Game* game, std::unordered_map<std::string, ParalaxMapInfo>& mappings) : PCCM(game)
{
	m_Mappings = mappings;

    for (auto& [mapName, info] : m_Mappings)
    {
        ParseDDSCubemap(info);
    }

    m_Intilized = true;
}

PCCM::~PCCM()
{
    SAFE_RELEASE(m_CubeMap);
    m_GenerationTexture.Release();
}

void PCCM::UploadPCCMMap(std::unordered_map<std::string, ParalaxMapInfo>& mappings)
{
    m_Mappings = mappings;

    for (auto& [mapName, info] : m_Mappings)
    {
        ParseDDSCubemap(info);
    }

    m_Intilized = true;
}

void PCCM::LoadCubeMap(const char* mapName)
{
    if (m_Mappings.empty()) 
    {
        m_Game->logMsg(LOGTYPE_WARNING, "No PCCM maps loaded.");
        return;
    }

	auto it = m_Mappings.find(mapName);
    if (it == m_Mappings.end())
    {
        m_Game->logMsg(LOGTYPE_WARNING, "%s is not mapped to any PCCM.", mapName);
        return;
    }
        
    ParalaxMapInfo& info = it->second;
    if (info.m_Error) 
    {
        m_Game->logMsg(LOGTYPE_ERROR, "Atempted to load PCCM that failed parseing, check logs.");
        return;
    }

    SAFE_RELEASE(m_CubeMap);
    HRESULT hr = m_DxDevice->CreateCubeTexture(info.m_Width, info.m_MipLevels, 0, info.m_Format, D3DPOOL_MANAGED, &m_CubeMap, nullptr);

    if (FAILED(hr))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "Failed creating PCCM cubemap for %s", mapName);
        return;
    }

    if (!UploadDDSCubemap(info)) 
    {
        SAFE_RELEASE(m_CubeMap);
        return;
    }

    m_Game->logMsg(LOGTYPE_DEBUG, "Loaded PCCM for &s", mapName);
}

void PCCM::ParseDDSCubemap(ParalaxMapInfo& info)
{
    std::ifstream file(info.m_ImagePath, std::ios::binary);

    if (!file.is_open())
    {
        m_Game->logMsg(LOGTYPE_ERROR, "Failed opening DDS: %s", info.m_ImagePath);
        info.m_Error = true;
        return;
    }

    char magic[4];
    file.read(magic, 4);

    if (memcmp(magic, "DDS ", 4) != 0)
    {
        m_Game->logMsg(LOGTYPE_ERROR, "%s is not a DDS file", info.m_ImagePath);
        info.m_Error = true;
        file.close();
        return;
    }

    DDS_HEADER header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(DDS_HEADER));

    info.m_Width = header.dwWidth;
    info.m_Height = header.dwHeight;
    info.m_MipLevels = header.dwMipMapCount;
    info.m_Format = D3DFMT_UNKNOWN;

    if (header.ddspf.dwFlags & DDPF_FOURCC)
    {
        switch (header.ddspf.dwFourCC)
        {
            case MAKEFOURCC('D', 'X', 'T', '1'):
            {
                info.m_Format = D3DFMT_DXT1;
                break;
            }
            case MAKEFOURCC('D', 'X', 'T', '3'):
            {
                info.m_Format = D3DFMT_DXT3;
                break;
            }
            case MAKEFOURCC('D', 'X', 'T', '5'):
            {
                info.m_Format = D3DFMT_DXT5;
                break;
            }
        }
    }
    else if (header.ddspf.dwFlags & DDPF_RGB)
    {
        if (header.ddspf.dwRGBBitCount == 32)
        {
            if (header.ddspf.dwRBitMask == 0x00ff0000 && header.ddspf.dwGBitMask == 0x0000ff00 && header.ddspf.dwBBitMask == 0x000000ff)
            {
                if (header.ddspf.dwABitMask == 0xff000000) info.m_Format = D3DFMT_A8R8G8B8;
                else info.m_Format = D3DFMT_X8R8G8B8;
            }
        }
    }

    if (info.m_Format == D3DFMT_UNKNOWN)
    {
        m_Game->logMsg(LOGTYPE_ERROR, "Unsupported DDS format: %s", info.m_ImagePath.c_str());
        info.m_Error = true;
        file.close();
        return;
    }

    file.close();
}

int PCCM::UploadDDSCubemap(ParalaxMapInfo& info)
{
    std::ifstream file(info.m_ImagePath, std::ios::binary);

    if (!file.is_open())
        return -1;


    //Skip DDS magic + header
    file.seekg(128, std::ios::beg);

    static const D3DCUBEMAP_FACES faces[6] =
    {
        D3DCUBEMAP_FACE_POSITIVE_X,
        D3DCUBEMAP_FACE_NEGATIVE_X,
        D3DCUBEMAP_FACE_POSITIVE_Y,
        D3DCUBEMAP_FACE_NEGATIVE_Y,
        D3DCUBEMAP_FACE_POSITIVE_Z,
        D3DCUBEMAP_FACE_NEGATIVE_Z
    };

    for (int face = 0; face < 6; face++)
    {
        UINT width = info.m_Width;
        UINT height = info.m_Height;

        for (UINT mip = 0; mip < info.m_MipLevels; mip++)
        {
            UINT size = GetMipSize(width, height, info.m_Format);
            std::vector<uint8_t> buffer(size);

            file.read(reinterpret_cast<char*>(buffer.data()), size);

            if (!file)
            {
                m_Game->logMsg(
                    LOGTYPE_ERROR,
                    "Failed reading DDS data: %s (EOF=%s, Fail=%s, Bad=%s)",
                    info.m_ImagePath.c_str(),
                    file.eof() ? "true" : "false",
                    file.fail() ? "true" : "false",
                    file.bad() ? "true" : "false"
                );

                return -1;
            }
                
            D3DLOCKED_RECT locked{};
            HRESULT hr = m_CubeMap->LockRect(faces[face], mip, &locked, nullptr, 0);

            if (FAILED(hr))
            {
                m_Game->logMsg(LOGTYPE_ERROR, "Failed to lock rect when uploading image to cube map. \nCode: %d", hr);
                file.close();
                return -1;
            }

            memcpy(locked.pBits, buffer.data(), size);

            m_CubeMap->UnlockRect(faces[face], mip);

            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
    }

    file.close();
    return 0;
}

void PCCM::SetupCubemapView(CViewSetup& view, int face)
{
    view.height = m_GenerationResolution;
    view.width = m_GenerationResolution;
    view.m_nUnscaledHeight = m_GenerationResolution;
    view.m_nUnscaledWidth = m_GenerationResolution;
    view.fov = 90.0f;
    view.fovViewmodel = 90.0f;
    view.m_flAspectRatio = 1.0f;

    static const Vector angles[6] =
    {
        Vector(0.0f, 0.0f, 0.0f),       // +X
        Vector(0.0f, 180.0f, 0.0f),     // -X
        Vector(0.0f, 90.0f, 0.0f),      // +Y
        Vector(0.0f, 270.0f, 0.0f),     // -Y
        Vector(-90.0f, 0.0f, 0.0f),     // +Z
        Vector(90.0f, 0.0f, 0.0f),      // -Z
    };

    view.angles = angles[face];
}

void PCCM::WriteImageToCubeMap(int face)
{
    if (m_GeneratingCubeMapState < PCCM_GENERATION_STATE_ENABLED || !m_GenerationTexture.m_ITex)
        return;

    if (m_GeneratingCubeMapState == PCCM_GENERATION_STATE_ENABLED)
    {
        SAFE_RELEASE(m_CubeMap);
        HRESULT hr = m_DxDevice->CreateCubeTexture(m_GenerationResolution, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_CubeMap, nullptr);

        if (FAILED(hr))
        {
            m_Game->logMsg(LOGTYPE_ERROR, "WriteImageToCubeMap: Failed to create cube map, code: %d", hr);
            m_GeneratingCubeMapState = PCCM_GENERATION_STATE_ERROR;
            return;
        }

        m_GeneratingCubeMapState = PCCM_GENERATION_STATE_CMTSET;
    }

    
    IDirect3DSurface9* dstSurface = nullptr;
    HRESULT hr = m_CubeMap->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), 0, &dstSurface);

    if (FAILED(hr))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "WriteImageToCubeMap: Failed getting cube face surface, Code: %d", hr);
        m_GeneratingCubeMapState = PCCM_GENERATION_STATE_ERROR;
        return;
    }

    hr = m_DxDevice->StretchRect(m_GenerationTexture.m_Surface, nullptr, dstSurface, nullptr, D3DTEXF_NONE);
    SAFE_RELEASE(dstSurface);

    if (FAILED(hr))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "WriteImageToCubeMap: Failed copying face %d: %d", face, hr);
        m_GeneratingCubeMapState = PCCM_GENERATION_STATE_ERROR;
    }
}

void PCCM::SaveImageToDSS(CViewSetup& view)
{
    if (!m_CubeMap || m_GeneratingCubeMapState < PCCM_GENERATION_STATE_CMTSET)
        return;

    std::string filename = GenerateCubeFileName(m_GenerationResolution);
    std::filesystem::path outputDir = std::filesystem::path(m_Game->m_GameDir) / "VR" / "PCCM";

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);

    if (ec)
    {
        m_Game->logMsg(LOGTYPE_ERROR, "SaveImageToDSS: failed to create PCCM file, Error: %s: %d", ec.message().c_str(), ec.value());
        return;
    }

    std::array<std::vector<uint8_t>, 6> faces;
    for (int face = 0; face < 6; face++)
    {
        IDirect3DSurface9* surface = nullptr;
        HRESULT hr = m_CubeMap->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(face), 0, &surface);

        if (FAILED(hr) || !surface)
        {
            m_Game->logMsg(LOGTYPE_ERROR, "SaveImageToDSS: failed to get cube map surface, Code: %d", hr);
            return;
        }

        D3DLOCKED_RECT rect{};
        hr = surface->LockRect(&rect, nullptr, D3DLOCK_READONLY);

        if (FAILED(hr))
        {
            m_Game->logMsg(LOGTYPE_ERROR, "SaveImageToDSS: failed to lock rect when reading cube map, Code: %d", hr);
            return;
        }

        faces[face].resize(m_GenerationResolution * m_GenerationResolution * 4);

        for (UINT y = 0; y < m_GenerationResolution; y++)
        {
            uint8_t* dst = faces[face].data() + y * m_GenerationResolution * 4;
            uint8_t* src = static_cast<uint8_t*>(rect.pBits) + y * rect.Pitch;

            memcpy(dst, src, m_GenerationResolution * 4);

            // Force opaque alpha
            for (UINT x = 0; x < m_GenerationResolution; x++)
            {
                dst[x * 4 + 3] = 255;
            }
        }

        surface->UnlockRect();
        surface->Release();
    }


    DirectX::ScratchImage cubeImage;
    DirectX::TexMetadata metadata{};

    metadata.width = m_GenerationResolution;
    metadata.height = m_GenerationResolution;
    metadata.depth = 1;
    metadata.arraySize = 6;
    metadata.mipLevels = 1;
    metadata.miscFlags = DirectX::TEX_MISC_TEXTURECUBE;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

    HRESULT hr = cubeImage.Initialize(metadata);

    if (FAILED(hr))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "SaveImageToDSS: failed to intilize cubeImage, Code: %d", hr);
        return;
    }

    for (size_t face = 0; face < 6; face++)
    {
        const DirectX::Image* image = cubeImage.GetImage(0, face, 0);
        memcpy(image->pixels, faces[face].data(), faces[face].size());
    }

    DirectX::ScratchImage compressed;

    hr = DirectX::Compress(
        cubeImage.GetImages(),
        cubeImage.GetImageCount(),
        cubeImage.GetMetadata(),
        DXGI_FORMAT_BC1_UNORM,
        DirectX::TEX_COMPRESS_DEFAULT,
        0.5f,
        compressed
    );

    if (FAILED(hr))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "SaveImageToDSS: failed to compress cube map image using DXT1, Code: %d", hr);
        return;
    }

    std::filesystem::path ddsPath = outputDir / (filename + ".dds");
    std::filesystem::path jsonPath = outputDir / (filename + ".json");
    std::wstring widePath(ddsPath.wstring());

    hr = DirectX::SaveToDDSFile(
        compressed.GetImages(),
        compressed.GetImageCount(),
        compressed.GetMetadata(),
        DirectX::DDS_FLAGS_NONE,
        widePath.c_str()
    );

    if (FAILED(hr))
    {
        m_Game->logMsg(LOGTYPE_ERROR, "SaveImageToDSS: failed to save DDS, Code: %d", hr);
        return;
    }

    SavePCCMJson(jsonPath, view);
}

std::string PCCM::GenerateCubeFileName(UINT resolution)
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_s(&tm, &time);

    std::stringstream ss;

    ss << resolution << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return ss.str();
}

void PCCM::SavePCCMJson(const std::filesystem::path& jsonPath, CViewSetup& view)
{
    // Need to calculate capture height from cam origin - player pos but can't do that yet
    float captureHeight = 64;

    nlohmann::json j;
    j["PCCM_Mappings"][""] =
    {
        { "image", "" },
        { "capturePosition", captureHeight },
        { "boxMin", { -512, -512, 0 } },
        { "boxMax", { 512, 512, 512 } }
    };

    std::ofstream file(jsonPath, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        m_Game->logMsg(LOGTYPE_ERROR, "SavePCCMJson: failed to open file");
        return;
    }

    file << j.dump(4);
    file.close();
}