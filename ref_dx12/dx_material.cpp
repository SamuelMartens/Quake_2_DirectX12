#include "dx_material.h"

#include "dx_app.h"

const std::string MaterialSource::STATIC_MATERIAL_NAME = "Static";
const std::string MaterialSource::DYNAMIC_MATERIAL_NAME = "Dynamic";


std::vector<MaterialSource> MaterialSource::ConstructSourceMaterials()
{
	std::vector<MaterialSource> materialSources;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC defaultPsoDesc;
	ZeroMemory(&defaultPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	defaultPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	//#DEBUG
	defaultPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	//END
	defaultPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	defaultPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	defaultPsoDesc.SampleMask = UINT_MAX;
	defaultPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	defaultPsoDesc.NumRenderTargets = 1;
	defaultPsoDesc.RTVFormats[0] = Renderer::QBACK_BUFFER_FORMAT;
	defaultPsoDesc.SampleDesc.Count = Renderer::Inst().GetMSAASampleCount();
	defaultPsoDesc.SampleDesc.Quality = Renderer::Inst().GetMSAAQuality();
	defaultPsoDesc.DSVFormat = Renderer::QDEPTH_STENCIL_FORMAT;


	// STATIC GEOM material -------------------
	MaterialSource& staticGeomMaterial = materialSources.emplace_back(MaterialSource());
	staticGeomMaterial.name = STATIC_MATERIAL_NAME;
	staticGeomMaterial.inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	staticGeomMaterial.psoDesc = defaultPsoDesc;
	staticGeomMaterial.psoDesc.InputLayout =
	{
		staticGeomMaterial.inputLayout.data(),
		static_cast<UINT>(staticGeomMaterial.inputLayout.size())
	};

	staticGeomMaterial.shaders[MaterialSource::ShaderType::Vs] = "vs_PosTex.cso";
	staticGeomMaterial.shaders[MaterialSource::ShaderType::Ps] = "ps_PosTex.cso";

	

	// Root signature is an array of root parameters
	staticGeomMaterial.rootParameters.resize(3);
	{
		// First parameter texture descriptor table
		staticGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& textureCbvTable = staticGeomMaterial.descriptorRanges.back();
		textureCbvTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		staticGeomMaterial.rootParameters[0].InitAsDescriptorTable(textureCbvTable.size(), textureCbvTable.data());

		// Second parameter is samplers descriptor table
		staticGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& samplersTable = staticGeomMaterial.descriptorRanges.back();

		samplersTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

		staticGeomMaterial.rootParameters[1].InitAsDescriptorTable(samplersTable.size(), samplersTable.data());

		// Third parameter in constant buffer view
		staticGeomMaterial.rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

	}

	staticGeomMaterial.rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// DYNAMIC GEOM material --------------
	MaterialSource& dynamicGeomMaterial = materialSources.emplace_back(MaterialSource());
	dynamicGeomMaterial.name = DYNAMIC_MATERIAL_NAME;

	dynamicGeomMaterial.inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"POSITION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	dynamicGeomMaterial.psoDesc = defaultPsoDesc;
	dynamicGeomMaterial.psoDesc.InputLayout =
	{
		dynamicGeomMaterial.inputLayout.data(),
		static_cast<UINT>(dynamicGeomMaterial.inputLayout.size())
	};

	dynamicGeomMaterial.shaders[MaterialSource::ShaderType::Vs] = "vs_PosPosTex.cso";
	dynamicGeomMaterial.shaders[MaterialSource::ShaderType::Ps] = "ps_PosTex.cso";

	dynamicGeomMaterial.rootParameters.resize(3);
	{
		// First parameter texture descriptor table
		dynamicGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& textureCbvTable = dynamicGeomMaterial.descriptorRanges.back();
		textureCbvTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		dynamicGeomMaterial.rootParameters[0].InitAsDescriptorTable(textureCbvTable.size(), textureCbvTable.data());

		// Second parameter is samplers descriptor table
		dynamicGeomMaterial.descriptorRanges.push_back(std::vector<CD3DX12_DESCRIPTOR_RANGE>(1));
		std::vector<CD3DX12_DESCRIPTOR_RANGE>& samplersTable = dynamicGeomMaterial.descriptorRanges.back();

		samplersTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

		dynamicGeomMaterial.rootParameters[1].InitAsDescriptorTable(samplersTable.size(), samplersTable.data());

		// Third parameter in constant buffer view
		dynamicGeomMaterial.rootParameters[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	}

	dynamicGeomMaterial.rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	return materialSources;
}

MaterialSource::MaterialSource()
{
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
}

