#pragma once

#include <string>
#include <d3d12.h>
#include <vector>

#include "d3dx12.h"
#include "dx_common.h"

class MaterialSource
{
public:

	enum ShaderType
	{
		Vs = 0,
		Ps = 1,
		SIZE
	};

	MaterialSource();

	MaterialSource(const MaterialSource&) = default;
	MaterialSource& operator=(const MaterialSource&) = default;

	MaterialSource(MaterialSource&&) = default;
	MaterialSource& operator=(MaterialSource&&) = default;

	~MaterialSource() = default;

	std::string name;
	std::string shaders[ShaderType::SIZE];

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	std::vector<CD3DX12_ROOT_PARAMETER> rootParameters;
	std::vector<CD3DX12_STATIC_SAMPLER_DESC> staticSamplers;
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
};

class MaterialCompiled
{
public:

	MaterialCompiled() = default;

	MaterialCompiled(const MaterialCompiled&) = default;
	MaterialCompiled& operator=(const MaterialCompiled&) = default;

	MaterialCompiled(MaterialCompiled&&) = default;
	MaterialCompiled& operator=(MaterialCompiled&&) = default;

	~MaterialCompiled() = default;

	std::string name;

	ComPtr<ID3D12PipelineState>		  pipelineState;
	ComPtr<ID3D12RootSignature>		  rootSingature;
};