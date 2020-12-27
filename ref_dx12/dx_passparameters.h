#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <variant>
#include <d3d12.h>
#include <memory>

#include "dx_common.h"
#include "dx_utils.h"
#include "dx_buffer.h"
#include "dx_commandlist.h"

namespace RootArg 
{
	struct RootConstant
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

	struct ConstBuffView
	{
		int index = Const::INVALID_INDEX;
		unsigned int hashedName = 0;
		std::vector<RootArg::ConstBuffField> content;
		// Doesn't own memory in this buffer, so no need to dealloc in destructor
		BufferPiece gpuMem;
	};

	struct DescTableEntity_ConstBufferView
	{
		unsigned int hashedName = 0;
		std::vector<RootArg::ConstBuffField> content;
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

	using DescTableEntity_t = std::variant<
		DescTableEntity_ConstBufferView,
		DescTableEntity_Texture,
		DescTableEntity_Sampler>;

	struct DescTable
	{
		DescTable() = default;

		DescTable(const DescTable& other);
		DescTable& operator=(const DescTable& other);

		DescTable(DescTable&& other);
		DescTable& operator=(DescTable&& other);

		~DescTable();

		int index = Const::INVALID_INDEX;
		std::vector<DescTableEntity_t> content;
		int viewIndex = Const::INVALID_INDEX;
	};

	using Arg_t = std::variant<RootConstant, ConstBuffView, DescTable>;

	void Bind(const Arg_t& rootArg, CommandList& commandList);

	int AllocateDescTableView(const DescTable& descTable);

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

};


namespace Parsing
{
	struct RootSignature;

	//#TODO implement PerFrame
	// Remove on init. This is actually not update? Rebind maybe?
	// Think about good name
	enum class ResourceUpdate
	{
		PerObject,
		PerPass,
		//	PerFrame,
		OnInit
	};

	enum class DataType
	{
		Float4x4,
		Float4,
		Float2,
		Int
	};


	unsigned int GetParseDataTypeSize(Parsing::DataType type);

	DXGI_FORMAT GetParseDataTypeDXGIFormat(Parsing::DataType type);

	struct VertAttrField
	{
		DataType type = DataType::Float4;
		unsigned int hashedName = 0;
		std::string semanticName;
		unsigned int semanticIndex = 0;
		// Need this for debug
		std::string name;
	};

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
		std::vector<RootArg::ConstBuffField> content;
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

	using  Resource_t = std::variant<
		Resource_ConstBuff, 
		Resource_Texture,
		Resource_Sampler>;
};


class PassParametersSource
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


	PassParametersSource();

	// The reason to remove copy functionality here is because when we initialize some structures
	// during material creation we pass pointers from descriptorRanges, which got invalidated
	// if you copy objects around
	PassParametersSource(const PassParametersSource&) = delete;
	PassParametersSource& operator=(const PassParametersSource&) = delete;

	PassParametersSource(PassParametersSource&&) = default;
	PassParametersSource& operator=(PassParametersSource&&) = default;

	~PassParametersSource() = default;

	static std::string_view GetResourceName(const Parsing::Resource_t& res);
	static std::string_view GetResourceRawView(const Parsing::Resource_t& res);

	static std::string ShaderTypeToStr(ShaderType type);

	std::string name;
	std::vector<ShaderSource> shaders;

	std::string colorTargetName;
	std::string depthTargetName;

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	InputType input = InputType::Undefined;

	std::vector<Parsing::VertAttr> vertAttr;
	std::vector<Parsing::Resource_t> resources;
	std::string inputVertAttr;
	std::vector<std::tuple<unsigned int, int>> vertAttrSlots;
	std::unique_ptr<Parsing::RootSignature> rootSignature;
};

class PassParameters
{
public:

	PassParameters() = default;

	PassParameters(const PassParameters&) = default;
	PassParameters& operator=(const PassParameters&) = default;

	PassParameters(PassParameters&&) = default;
	PassParameters& operator=(PassParameters&&) = default;

	~PassParameters() = default;

public:

	/* Shared */

	std::string name;

	unsigned int colorTargetNameHash = 0;
	unsigned int depthTargetNameHash = 0;

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };


	std::vector<RootArg::Arg_t> perObjectRootArgsTemplate;

	// I do need this if I want callback style handling of vertex attributes
	// Maybe not everything from this tho. Like rawView is not required. 
	// Will refactor this if I will ever do callbacks for vertex attribute handling
	//#DEBUG rework this on something that I actually need
	Parsing::VertAttr vertAttr;

	//#TODO this doesn't belongs to pass, as pass type is what defines information for that
	PassParametersSource::InputType input = PassParametersSource::InputType::Undefined;

	ComPtr<ID3D12PipelineState>		  pipelineState;
	ComPtr<ID3D12RootSignature>		  rootSingature;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	/*  Owned by pass */

	std::vector<RootArg::Arg_t> passRootArgs;

};