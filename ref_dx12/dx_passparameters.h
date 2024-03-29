#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <variant>
#include <d3d12.h>
#include <memory>
#include <optional>
#include <tuple>

#include "dx_common.h"
#include "dx_utils.h"
#include "dx_buffer.h"
#include "dx_commandlist.h"
#include "dx_settings.h"
#include "dx_memorymanager.h"

/* --- HOW TO ADD NEW TYPE OF ROOT ARGUMENT
* 
* 1) Modify grammar to parse new type
* 
* 2) Modify parser code inside dx_framegraphbuilder.cpp to have callbacks for the 
*	new grammar
* 
* 3) Add new type to Parsing::RootParam if root signature changes are required
* 
* 4) Add new type to Parsing::Resource_t 
* 
* 5) Add new RootArg 
* 
* 6) Add new RootArg creation code to CreateRootArgument()
* 
* 7) Add Registration/Update code for GLOBAL type of resource to dx_framegraph
*	(make sure to handle Internal resources if needed)
* 
* 8) Add Registration/Update code for LOCAL type of resources to dx_pass
*	(make sure to handle Internal resources if needed)
* 
* 9) If new root argument type requires new DX resource, add modify
		ResourceManager to create new resource
* 
*/

namespace Parsing
{
	enum class PassInputType;
};


namespace RootArg 
{
	struct RootConstant
	{
		unsigned int size = Const::INVALID_SIZE;
		unsigned int hashedName = 0;
	};

	struct ConstBuffField
	{
		unsigned int size = Const::INVALID_SIZE;
		unsigned int hashedName = 0;

		bool IsEqual(const ConstBuffField& other) const;
	};

	struct ConstBuffView
	{
		unsigned int hashedName = 0;
		std::vector<RootArg::ConstBuffField> content;
		// Doesn't own memory in this buffer, so no need to dealloc in destructor
		BufferPiece gpuMem;
	};
	
	struct UAView
	{
		unsigned int hashedName = 0;

		std::optional<std::string> internalBindName;

		// Doesn't own memory in this buffer, so no need to dealloc in destructor
		BufferPiece gpuMem;
	};

	struct StructuredBufferView
	{
		unsigned int hashedName = 0;

		std::optional<std::string> internalBindName;
		std::optional<int> strideInBytes;

		Resource* buffer = nullptr;
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

		std::optional<std::string> internalBindName;
	};

	struct DescTableEntity_Sampler
	{
		unsigned int hashedName = 0;
	};

	struct DescTableEntity_UAView
	{
		unsigned int hashedName = 0;

		std::optional<std::string> internalBindName;
		std::optional<int> strideInBytes;
	};

	struct DescTableEntity_StructuredBufferView
	{
		unsigned int hashedName = 0;

		std::optional<std::string> internalBindName;
		std::optional<int> strideInBytes;
	};

	using DescTableEntity_t = std::variant<
		DescTableEntity_ConstBufferView,
		DescTableEntity_Texture,
		DescTableEntity_Sampler,
		DescTableEntity_UAView,
		DescTableEntity_StructuredBufferView>;

	struct DescTable
	{
		DescTable() = default;

		DescTable(const DescTable& other);
		DescTable& operator=(const DescTable& other);

		DescTable(DescTable&& other);
		DescTable& operator=(DescTable&& other);

		~DescTable() = default;

		std::vector<DescTableEntity_t> content;
		int viewIndex = Const::INVALID_INDEX;
	};

	using Arg_t = std::variant<RootConstant, ConstBuffView, UAView, StructuredBufferView, DescTable>;

	struct GlobalArgRef
	{
		int globalListIndex = Const::INVALID_INDEX;
		int bindIndex = Const::INVALID_INDEX;
	};

	struct LocalArg
	{
		Arg_t arg;
		int bindIndex = Const::INVALID_INDEX;
	};

	int FindArg(const std::vector<Arg_t>& args, const Arg_t& arg);

	void Bind(const Arg_t& rootArg, const int bindIndex, CommandList& commandList);
	void BindCompute(const Arg_t& rootArg, const int bindIndex, CommandList& commandList);

	template<typename T>
	int AllocateDescTableView(const DescTable& descTable, T& allocator)
	{ 
		DX_ASSERT(descTable.content.empty() == false && "Allocation view error. Desc table content can't be empty.");

		// Figure what kind of desc hep to use from inspection of the first element
		return std::visit([size = descTable.content.size(), &allocator](auto&& descTableEntity)
		{
			using T = std::decay_t<decltype(descTableEntity)>;

			if constexpr (
				std::is_same_v<T, DescTableEntity_ConstBufferView> ||
				std::is_same_v<T, DescTableEntity_Texture> ||
				std::is_same_v<T, DescTableEntity_UAView> ||
				std::is_same_v<T, DescTableEntity_StructuredBufferView>)
			{
				return allocator.AllocateRange(size);
			}
			else if constexpr (std::is_same_v<T, DescTableEntity_Sampler>)
			{
				// Samplers are hard coded to 0 for now. Always use 0
				// Samplers live in descriptor heap, which makes things more complicated, because
				// I loose one of indirections that I rely on.
				return 0;
			}
			else
			{
				DX_ASSERT(false && "AllocateDescTableView error. Unknown type of desc table entity ");
			}

			return -1;

		}, descTable.content[0]);
	}

	template<typename T>
	void ReleaseDescTableView(DescTable& descTable, T& allocator)
	{
		DX_ASSERT(descTable.content.empty() == false && "DesctTable release error. Desc table content can't be empty.");

		std::visit([&allocator, &descTable](auto&& desctTableEntity)
		{
			using T = std::decay_t<decltype(desctTableEntity)>;

			//#TODO fix this when samplers are handled properly
			if constexpr (std::is_same_v<T, DescTableEntity_Sampler> == false)
			{
				if (descTable.viewIndex != Const::INVALID_INDEX)
				{
					allocator.DeleteRange(descTable.viewIndex, descTable.content.size());
				}
			}

		}, descTable.content[0]);
	}

	template<typename T>
	void Release(Arg_t& arg, T& allocator)
	{
		std::visit([&allocator](auto&& arg) 
		{
			using T = std::decay_t<decltype(arg)>;

			if constexpr (std::is_same_v<T, DescTable>)
			{
				ReleaseDescTableView(arg, allocator);
			}

		}, arg);
	}

	void AttachConstBufferToArgs(std::vector<RootArg::Arg_t>& rootArgs, int offset, BufferHandler gpuHandler);
	void AttachConstBufferToLocalArgs(std::vector<RootArg::LocalArg>& rootLocalArgs, int offset, BufferHandler gpuHandler);
	void AttachConstBufferToArg(RootArg::Arg_t& rootArg, int& offset, BufferHandler gpuHandler);

	template<typename T>
	int GetConstBufftSize(const T& cb)
	{
		int size = 0;

		std::for_each(cb.content.begin(), cb.content.end(),
			[&size](const ConstBuffField& f)
		{
			size += f.size;
		});

		return Utils::Align(size, Settings::CONST_BUFFER_ALIGNMENT);
	}

	int GetDescTableSize(const DescTable& descTable);
	int GetArgSize(const Arg_t& arg);

	int GetLocalArgsSize(const std::vector<RootArg::LocalArg>& args);
	int GetArgsSize(const std::vector<RootArg::Arg_t>& args);

	template<typename... Ts>
	int GetArgsSize(const std::tuple<Ts...>& args) 
	{
		return std::apply([](Ts... args) 
		{
			return (GetArgsSize(args) + ...);
		}, args);
	}
};


namespace Parsing
{
	struct RootSignature;

	enum class ResourceBindFrequency
	{
		PerObject,
		PerPass
	};

	enum class ResourceScope
	{
		Local,
		Global
	};

	enum class DataType
	{
		Float4x4,
		Float4,
		Float2,
		Float,
		Int
	};

	enum class PassInputType
	{
		Static,
		Dynamic,
		Particles,
		UI,
		PostProcess,
		Debug,
		SIZE
	};

	unsigned int GetParseDataTypeSize(Parsing::DataType type);

	DXGI_FORMAT GetParseDataTypeDXGIFormat(Parsing::DataType type);

	struct Function
	{
		std::string name;
		std::string rawView;
	};

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

	struct StructField
	{
		DataType type = DataType::Float4;
		unsigned int hashedName = 0;
		// Need this for debug
		std::string name;
	};

	struct MiscDef_Struct
	{
		std::string name;
		std::vector<StructField> content;
		std::string rawView;
	};

	using MiscDef_t = std::variant<
		MiscDef_Struct
	>;

	struct Resource_Base
	{
		std::string name;

		std::optional<ResourceBindFrequency> bindFrequency;
		std::optional<ResourceScope> scope;
		std::optional<std::string> bind;

		int registerId = Const::INVALID_INDEX;
		int registerSpace = Const::INVALID_INDEX;
		std::string rawView;
		
		bool IsEqual(const Resource_Base& other) const;
	};

	struct Resource_ConstBuff : public Resource_Base
	{
		std::vector<RootArg::ConstBuffField> content;
		
		bool IsEqual(const Resource_ConstBuff& other) const;
	};

	struct Resource_Texture : public Resource_Base
	{};

	struct Resource_RWTexture : public Resource_Base
	{};

	struct Resource_Sampler : public Resource_Base
	{};

	// Either some struct or basic type
	using StructBufferDataType_t = std::variant<std::string,
		DataType>;

	struct Resource_StructuredBuffer : public Resource_Base
	{
		StructBufferDataType_t dataType;

		bool IsEqual(const Resource_StructuredBuffer& other) const;
	};

	struct Resource_RWStructuredBuffer : public Resource_StructuredBuffer
	{};

	using  Resource_t = std::variant<
		Resource_ConstBuff, 
		Resource_Texture,
		Resource_Sampler,
		Resource_RWTexture,
		Resource_StructuredBuffer,
		Resource_RWStructuredBuffer
	>;

	bool IsEqual(const Resource_t& res1, const Resource_t& res2);

	std::string_view GetResourceName(const Parsing::Resource_t& res);
	std::string_view GetResourceRawView(const Parsing::Resource_t& res);
	Parsing::ResourceBindFrequency GetResourceBindFrequency(const Parsing::Resource_t& res);
	Parsing::ResourceScope GetResourceScope(const Resource_t& res);

	std::string_view GetMiscDefName(const Parsing::MiscDef_t& def);
	std::string_view GetMiscDefRawView(const Parsing::MiscDef_t& def);

	int GetVertAttrSize(const VertAttr& vertAttr);
	int GetStructBufferDataTypeSize(const StructBufferDataType_t& dataType);
};


class PassParametersSource
{
public:

	enum ShaderType
	{
		Vs = 0,
		Gs = 1,
		Ps = 2,
		Cs = 3,
		SIZE
	};

	struct ShaderSource
	{
		ShaderType type;
		std::vector<std::string> externals;
		std::string source;
	};

	struct FixedFunctionClearColor
	{
		XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 1.0f };
	};

	struct FixedFunctionClearDepth
	{
		float value = 1.0f;
	};

	using FixedFunction_t = std::variant<FixedFunctionClearColor, FixedFunctionClearDepth>;

	PassParametersSource();

	// The reason to remove copy functionality here is because when we initialize some structures
	// during material creation we pass pointers from descriptorRanges, which got invalidated
	// if you copy objects around
	PassParametersSource(const PassParametersSource&) = delete;
	PassParametersSource& operator=(const PassParametersSource&) = delete;

	PassParametersSource(PassParametersSource&&) = default;
	PassParametersSource& operator=(PassParametersSource&&) = default;

	~PassParametersSource() = default;

	static std::string ShaderTypeToStr(ShaderType type);

	std::string name;
	std::vector<ShaderSource> shaders;

	std::vector<std::string> colorTargetNames;
	std::vector<DXGI_FORMAT> colorTargetFormats;

	std::string depthTargetName;

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	D3D12_GRAPHICS_PIPELINE_STATE_DESC rasterPsoDesc;
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	std::optional<Parsing::PassInputType> input;
	std::optional<std::array<int, 3>> threadGroups;

	std::vector<Parsing::Function> functions;
	std::vector<Parsing::VertAttr> vertAttr;
	std::vector<Parsing::Resource_t> resources;
	std::vector<Parsing::MiscDef_t> miscDefs;
	std::string inputVertAttr;
	std::vector<std::tuple<unsigned int, int>> vertAttrSlots;
	std::unique_ptr<Parsing::RootSignature> rootSignature;

	std::vector<FixedFunction_t> prePassFuncs;
	std::vector<FixedFunction_t> postPassFuncs;
};

class PassParameters
{
public:

	struct RenderTarget
	{
		std::string name;
		int viewIndex = Const::INVALID_INDEX;
	};

public:

	// I need this as constexpr, that's why cannot use std::string
	// I also need this as c string. So can't use string_vew either
	// This leave me not a lot of options
	static constexpr const char* const COLOR_BACK_BUFFER_NAME = "COLOR_BACK_BUFFER";
	static constexpr const char* const DEPTH_BACK_BUFFER_NAME = "DEPTH_BACK_BUFFER";

	PassParameters() = default;

	PassParameters(const PassParameters&) = default;
	PassParameters& operator=(const PassParameters&) = default;

	PassParameters(PassParameters&&) = default;
	PassParameters& operator=(PassParameters&&) = default;

	~PassParameters() = default;

	static void AddGlobalPerObjectRootArgRef(
		std::vector<RootArg::GlobalArgRef>& perObjGlobalRootArgsRefsTemplate,
		std::vector<RootArg::Arg_t>& perObjGlobalResTemplate,
		int bindIndex,
		RootArg::Arg_t&& arg);

public:

	/* Shared */

	std::string name;

	std::vector<RenderTarget> colorRenderTargets;

	RenderTarget depthRenderTarget;

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

	std::vector<RootArg::LocalArg> perObjectLocalRootArgsTemplate;
	// Indices of global RootArgs in objects global RootArg storage that are needed for this pass
	std::vector<RootArg::GlobalArgRef> perObjGlobalRootArgsRefsTemplate;

	std::optional<Parsing::VertAttr> vertAttr;

	std::optional<Parsing::PassInputType> input;
	std::optional<std::array<int, 3>> threadGroups;

	ComPtr<ID3D12PipelineState>		  pipelineState;
	ComPtr<ID3D12RootSignature>		  rootSingature;

	D3D_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	/*  Owned by pass */

	std::vector<RootArg::LocalArg> passLocalRootArgs;
	std::vector<RootArg::GlobalArgRef> passGlobalRootArgsRefs;

};