#pragma once

#include <string>
#include <d3d12.h>
#include <vector>

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