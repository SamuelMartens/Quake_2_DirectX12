#pragma once

#include <string>
#include <d3d12.h>
#include <vector>
#include <variant>
#include <string_view>

#include "d3dx12.h"
#include "dx_common.h"

class MaterialSource
{
public:

	static const std::string STATIC_MATERIAL_NAME;
	static const std::string DYNAMIC_MATERIAL_NAME;
	static const std::string PARTICLE_MATERIAL_NAME;

	static std::vector<MaterialSource> ConstructSourceMaterials();

public:

	enum ShaderType
	{
		Vs = 0,
		Ps = 1,
		Gs = 2,
		SIZE
	};

	MaterialSource();

	// The reason to remove copy functionality here is because when we initialize some structures
	// during material creation we pass pointers from descriptorRanges, which got invalidated
	// if you copy objects around
	MaterialSource(const MaterialSource&) = delete;
	MaterialSource& operator=(const MaterialSource&) = delete;

	MaterialSource(MaterialSource&&) = default;
	MaterialSource& operator=(MaterialSource&&) = default;

	~MaterialSource() = default;

	std::string name;
	std::string shaders[ShaderType::SIZE];

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	// I need to keep this object in memory until serialization
	std::vector<std::vector<CD3DX12_DESCRIPTOR_RANGE>> descriptorRanges;

	std::vector<CD3DX12_ROOT_PARAMETER> rootParameters;
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> staticSamplers;
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

class Material
{
public:

	Material() = default;

	Material(const Material&) = default;
	Material& operator=(const Material&) = default;

	Material(Material&&) = default;
	Material& operator=(Material&&) = default;

	~Material() = default;

	std::string name;

	ComPtr<ID3D12PipelineState>		  pipelineState;
	ComPtr<ID3D12RootSignature>		  rootSingature;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

//#DEBUG find proper place for this
//#DEBUG in all resource it should really be "rawView" and the rest of stuff is maintained by "string_view"
struct Resource_VertAttr
{
	std::string name;
	std::string content;
	std::string rawView;
};

struct Resource_ConstBuff
{
	std::string name;
	std::string registerName;
	std::string content;
	std::string rawView;
};

struct Resource_Texture
{
	std::string name;
	std::string registerName;
	std::string rawView;
};

struct Resource_Sampler
{
	std::string name;
	std::string registerName;
	std::string rawView;
};

using  Resource_t = std::variant<Resource_VertAttr, Resource_ConstBuff, Resource_Texture, Resource_Sampler>;

class PassSource
{
public:

	enum ShaderType
	{
		Vs = 0,
		Gs = 1,
		Ps = 2,
		SIZE
	};

	enum class InputType
	{
		Static,
		Dynamic,
		Particles,
		UI,
		PostProcess,
		Undefined
	};
	
	enum class ResourceUpdate
	{
		PerObject,
		PerPass,
		PerFrame,
		OnInit
	};

	enum class ResourceScope
	{
		Local,
		Global
	};

	struct ShaderSource
	{
		ShaderType type;
		std::vector<std::string> externals;
		std::string source;
	};


	PassSource();

	// The reason to remove copy functionality here is because when we initialize some structures
	// during material creation we pass pointers from descriptorRanges, which got invalidated
	// if you copy objects around
	PassSource(const PassSource&) = delete;
	PassSource& operator=(const PassSource&) = delete;

	PassSource(PassSource&&) = default;
	PassSource& operator=(PassSource&&) = default;

	~PassSource() = default;


	template<typename T>
	static std::string_view _GetResourceName(const T& res)
	{
		return res.name;
	}

	template<typename T>
	static std::string_view _GetResourceRawView(const T& res)
	{
		return res.rawView;
	}

	static std::string_view GetResourceName(const Resource_t& res);
	static std::string_view GetResourceRawView(const Resource_t& res);

	static std::string ShaderTypeToStr(ShaderType& type);

	std::string name;
	std::vector<ShaderSource> shaders;

	std::string colorTargetName;
	std::string depthTargetName;

	D3D12_VIEWPORT viewport = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	// I need to keep this object in memory until serialization
	std::vector<std::vector<CD3DX12_DESCRIPTOR_RANGE>> descriptorRanges;

	std::vector<CD3DX12_ROOT_PARAMETER> rootParameters;
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> staticSamplers;
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	InputType input = InputType::Undefined;

	std::vector<Resource_t> resources;
};

//#DEBUG findt proper place for this as well
struct ParseContext
{
	std::vector<PassSource> passSources;
	std::vector<Resource_t> resources;
};