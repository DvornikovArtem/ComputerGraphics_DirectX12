#pragma once

#ifndef DESCRIPTORS_H
#define DESCRIPTORS_H

#include <string>
#include <d3dcommon.h>
#include <Engine/Math/SimpleMath.h>

using namespace DirectX;

struct ShaderDesc
{
    ShaderDesc() {}

    ShaderDesc(std::string Name, std::wstring Path, std::string FunctionName, const D3D_SHADER_MACRO* ShaderDefines, std::string ShaderProfile)
    {
        this->Name = Name;
        this->Path = Path;
        this->FunctionName = FunctionName;
        this->ShaderDefines = ShaderDefines;
        this->ShaderProfile = ShaderProfile;
    }

    ~ShaderDesc() = default;

    std::string Name;
    std::wstring Path;
    std::string FunctionName;
    const D3D_SHADER_MACRO* ShaderDefines;
    std::string ShaderProfile;
};

struct TextureDesc
{
    enum TextureType { Texture2D, CubeMap };
    TextureDesc() {}

    TextureDesc(std::string Name, std::wstring Path, TextureType TexType, bool UseSRGB)
    {
        this->Name = Name;
        this->Path = Path;
        this->TexType = TexType;
        this->UseSRGB = UseSRGB;
    }

    ~TextureDesc() = default;

    std::string Name;
    std::wstring Path;
    TextureType TexType;
    bool UseSRGB;
};

struct MaterialDesc
{
    MaterialDesc() {}

    MaterialDesc(std::string Name, std::string VertexShaderName, std::string PixelShaderName, std::string HullShaderName, std::string DomainShaderName, std::string DiffuseTexName, std::string NormalMapName, std::string HeightMapName, XMFLOAT4 DiffuseAlbedo, XMFLOAT3 FresnelR0, float Roughness, float Metallic, bool UseTesselation)
    {
        this->Name = Name;
        this->VertexShaderName = VertexShaderName;
        this->PixelShaderName = PixelShaderName;
        this->HullShaderName = HullShaderName;
        this->DomainShaderName = DomainShaderName;
        this->DiffuseTexName = DiffuseTexName;
        this->NormalMapName = NormalMapName;
        this->HeightMapName = HeightMapName;
        this->DiffuseAlbedo = DiffuseAlbedo;
        this->FresnelR0 = FresnelR0;
        this->Roughness = Roughness;
        this->Metallic = Metallic;
        this->UseTesselation = UseTesselation;
    }

    ~MaterialDesc() = default;

    std::string Name = "";
    std::string DiffuseTexName = "";
    std::string NormalMapName = "";
    std::string HeightMapName = "";
    XMFLOAT4 DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    XMFLOAT3 FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    float Roughness = 0.f;
    float Metallic = 0.f;
    std::string PixelShaderName = "standardPS";
    std::string VertexShaderName = "standardVS";
    std::string HullShaderName = "standardHS";
    std::string DomainShaderName = "standardDS";
    bool UseTesselation = false;
};

struct MeshDesc
{
    enum ImportType { LODed, Complex };

    MeshDesc() {}

    MeshDesc(std::string Name, std::string Path, ImportType importType)
    {
        this->Name = Name;
        this->Path = Path;
        this->importType = importType;
    }

    ~MeshDesc() = default;

    std::string Name;
    std::string Path;
    std::string TextureName = "";
    ImportType importType;
};

struct MeshParsingResult
{
    MeshParsingResult() {}

    ~MeshParsingResult() = default;

    std::string GeometryName = "";
    std::string DiffuseTextureName = "";
    std::string NormalMapName = "";
    std::string RoughnessMapName = "";
    MaterialDesc GeneratedMaterial;
    bool GenerateMaterial;
};

#endif // RENDERINGSYSTEM_H