#pragma once

#include <string>
#include <d3d12.h>
#include <vector>
#include <variant>
#include <string_view>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_utils.h"
#include "dx_buffer.h"
#include "dx_commandlist.h"

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

namespace RootSigParseData
{
	enum class Option
	{
		Visibility,
		NumDecl
	};

	enum class DescViewType
	{
		ConstBuff,
		TextView,
		SamplerView
	};

	struct RootParam_RootConst
	{
		int num32BitConsts = Const::INVALID_SIZE;
		int registerId = Const::INVALID_INDEX;
	};

	// All these views can be used for both inlinde descriptor declaration and
	// as a part of descriptor table
	struct RootParam_ConstBuffView
	{
		int registerId = Const::INVALID_INDEX;
		int num = Const::INVALID_SIZE;
	};

	struct RootParam_TextView
	{
		int registerId = Const::INVALID_INDEX;
		int num = Const::INVALID_SIZE;
	};

	struct RootParam_SamplerView
	{
		int registerId = Const::INVALID_INDEX;
		int num = Const::INVALID_SIZE;
	};

	using DescTableEntity_t = std::variant<RootParam_ConstBuffView, RootParam_TextView, RootParam_SamplerView>;

	struct RootParam_DescTable
	{
		std::vector<DescTableEntity_t> entities;
	};

	using RootParma_t = std::variant<RootParam_ConstBuffView, RootParam_DescTable>;
	//#DEBUG super IMPORTANT! handle views free
	struct RootSignature
	{
		std::vector<RootParma_t> params;
		std::string rawView;
	};
};

//#DEBUG implement PerFrame
// Remove on init. This is actually not update? Rebind maybe?
// Think about good name
enum class ResourceUpdate
{
	PerObject,
	PerPass,
//	PerFrame,
	OnInit
};


enum class ParseDataType
{
	Float4x4,
	Float4,
	Float2,
	Int
};

unsigned int GetParseDataTypeSize(ParseDataType type);
DXGI_FORMAT GetParseDataTypeDXGIFormat(ParseDataType type);

struct VertAttrField
{
	ParseDataType type = ParseDataType::Float4;
	unsigned int hashedName = 0;
	std::string semanticName;
	unsigned int semanticIndex = 0;
	// Need this for debug
	std::string name;
}; 
 
struct RootArg_RootConstant
{
	int index = Const::INVALID_INDEX;
	unsigned int size = Const::INVALID_SIZE;
	unsigned int hashedName = 0;
};

struct ConstBuffField 
{
	unsigned int size = Const::INVALID_SIZE;
 	unsigned int hashedName = 0;
};

struct RootArg_ConstBuffView
{
	int index = Const::INVALID_INDEX;
	unsigned int hashedName = 0;
	std::vector<ConstBuffField> content;
	// Doesn't own memory in this buffer, so no need to dealloc in destructor
	BufferPiece gpuMem;
};

struct DescTableEntity_ConstBufferView
{
	unsigned int hashedName = 0;
	std::vector<ConstBuffField> content;
	BufferPiece gpuMem;
};

struct DescTableEntity_Texture
{
	unsigned int hashedName = 0;
};

struct DescTableEntity_Sampler
{
	unsigned int hashedName = 0;
};

using DescTableEntity_t = std::variant<DescTableEntity_ConstBufferView, DescTableEntity_Texture, DescTableEntity_Sampler>;

struct RootArg_DescTable
{
	RootArg_DescTable() = default;

	RootArg_DescTable(const RootArg_DescTable& other);
	RootArg_DescTable& operator=(const RootArg_DescTable& other);

	RootArg_DescTable(RootArg_DescTable&& other);
	RootArg_DescTable& operator=(RootArg_DescTable&& other);

	~RootArg_DescTable();

	int index = Const::INVALID_INDEX;
	std::vector<DescTableEntity_t> content;
	int viewIndex = Const::INVALID_INDEX;
};

using RootArg_t = std::variant<RootArg_RootConstant, RootArg_ConstBuffView, RootArg_DescTable>;

void BindRootArg(const RootArg_t& rootArg, CommandList& commandList);

int AllocateDescTableView(const RootArg_DescTable& descTable);



//#DEBUG not sure if rawView is really needed. 
// SFINAE can help solve problem if I don't need it for some members
// This shows what is the name name of res to bind
struct VertAttr
{
	std::string name;
	std::vector<VertAttrField> content;
	std::string rawView;
};

struct Resource_ConstBuff
{
	std::string name;
	ResourceUpdate updateFrequency = ResourceUpdate::OnInit;
	int registerId = Const::INVALID_INDEX;
	std::vector<ConstBuffField> content;
	std::string rawView;
};

struct Resource_Texture
{
	std::string name;
	ResourceUpdate updateFrequency = ResourceUpdate::OnInit;
	int registerId = Const::INVALID_INDEX;
	std::string rawView;
};

struct Resource_Sampler
{
	std::string name;
	ResourceUpdate updateFrequency = ResourceUpdate::OnInit;
	int registerId = Const::INVALID_INDEX;
	std::string rawView;
};

using  Resource_t = std::variant<Resource_ConstBuff, Resource_Texture, Resource_Sampler>;


template<typename T>
int GetConstBuffSize(const T& cb)
{
	int size = 0;

	std::for_each(cb.content.begin(), cb.content.end(),
		[&size](const ConstBuffField& f)
	{
		size += f.size;
	});

	return Utils::Align(size, Settings::CONST_BUFFER_ALIGNMENT);
}


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

	static std::string_view GetResourceName(const Resource_t& res);
	static std::string_view GetResourceRawView(const Resource_t& res);

	static std::string ShaderTypeToStr(ShaderType type);

	std::string name;
	std::vector<ShaderSource> shaders;

	std::string colorTargetName;
	std::string depthTargetName;

	D3D12_VIEWPORT viewport = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	InputType input = InputType::Undefined;

	std::vector<VertAttr> vertAttr;
	std::vector<Resource_t> resources;
	std::string inputVertAttr;
	std::vector<std::tuple<unsigned int, int>> vertAttrSlots;
	RootSigParseData::RootSignature rootSignature;
};

//#DEBUG findt proper place for this as well
struct ParsePassContext
{
	std::vector<PassSource> passSources;
	std::vector<Resource_t> resources;
};

struct ParseMaterialContext
{
	std::vector<std::string> passes;
};

class Pass
{
public:

	Pass() = default;

	Pass(const Pass&) = default;
	Pass& operator=(const Pass&) = default;

	Pass(Pass&&) = default;
	Pass& operator=(Pass&&) = default;

	~Pass() = default;

public:

	/* Shared */

	std::string name;

	unsigned int colorTargetNameHash = 0;
	unsigned int depthTargetNameHash = 0;

	D3D12_VIEWPORT viewport = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};


	std::vector<RootArg_t> perObjectRootArgsTemplate;

	// I do need this if I want callback style handling of vertex attributes
	// Maybe not everything from this tho. Like rawView is not required. 
	// Will refactor this if I will ever do callbacks for vertex attribute handling
	//#DEBUG rework this on something that I actually need
	VertAttr vertAttr;

	//#TODO this doesn't belongs to pass, as stage type is what defines information for that
	PassSource::InputType input = PassSource::InputType::Undefined;

	ComPtr<ID3D12PipelineState>		  pipelineState;
	ComPtr<ID3D12RootSignature>		  rootSingature;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	/*  Owned by pass */

	std::vector<RootArg_t> passRootArgs;

};


//#DEBUG rename this to just materials
class PassMaterial
{
public:

	PassMaterial() = default;

	PassMaterial(const PassMaterial&) = default;
	PassMaterial& operator=(const PassMaterial&) = default;

	PassMaterial(PassMaterial&&) = default;
	PassMaterial& operator=(PassMaterial&&) = default;

	~PassMaterial() = default;

	std::vector<Pass> passes;
};