#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

#include "dx_passparameters.h"
#include "dx_framegraph.h"
#include "dx_resource.h"

namespace Parsing
{
	enum class Option
	{
		Visibility,
		NumDecl,
		Space
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

	// All these views can be used for both inline descriptor declaration and
	// as a part of descriptor table
	struct RootParam_BaseView
	{
		int registerId = Const::INVALID_INDEX;
		int registerSpace = Const::INVALID_INDEX;
		int num = Const::INVALID_SIZE;
	};

	struct RootParam_ConstBuffView : public RootParam_BaseView
	{};

	struct RootParam_ShaderResourceView : public RootParam_BaseView
	{};

	struct RootParam_SamplerView : public RootParam_BaseView
	{};

	struct RootParam_UAView : public RootParam_BaseView
	{};

	using DescTableEntity_t = std::variant<
		RootParam_ConstBuffView,
		RootParam_ShaderResourceView,
		RootParam_SamplerView,
		RootParam_UAView>;

	struct RootParam_DescTable
	{
		std::vector<DescTableEntity_t> entities;
	};

	using RootParma_t = std::variant<
		RootParam_ConstBuffView,
		RootParam_ShaderResourceView,
		RootParam_DescTable,
		RootParam_UAView>;
	
	struct RootSignature
	{
		std::vector<RootParma_t> params;
		std::string rawView;
	};

	struct FrameGraphSourceContext
	{
		std::vector<FrameGraphSource::Step_t> steps;

		std::vector<FrameGraphSource::FrameGraphResourceDecl> resources;
	};

	struct PassParametersContext
	{
		std::vector<PassParametersSource> passSources;

		const Parsing::FrameGraphSourceContext* frameGraphContext = nullptr;
	};
}

class FrameGraphBuilder
{
public:
	
	struct CompiledShaderData
	{
		std::optional<PassParametersSource::ShaderType> type;

		ComPtr<ID3DBlob> shaderBlob;
		// Root signature needs to be in separate blob because I specify it inside pass file, and if I just include it threre, sometimes
		// it behaves very funky. This way it is much safer
		ComPtr<ID3DBlob> rootSigBlob;
	};

	DEFINE_SINGLETON(FrameGraphBuilder);


	bool IsSourceChanged();
	void BuildFrameGraph(std::unique_ptr<FrameGraph>& outFrameGraph, std::vector<FrameGraphSource::FrameGraphResourceDecl>& internalResourceDecl);

	void CreateFrameGraphResources(const std::vector<FrameGraphSource::FrameGraphResourceDecl>& resourceDecls, FrameGraph& frameGraph) const;
	std::vector<ResourceProxy> CreateFrameGraphTextureProxies(const std::vector<std::string>& internalTextureList) const;

private:

	/* FrameGraph generation */
	FrameGraphSource GenerateFrameGraphSource() const;

	[[nodiscard]]
	FrameGraph CompileFrameGraph(FrameGraphSource&& source) const;

	/* Internal resources */
	std::vector<std::string> CreateFrameGraphResources(const std::vector<FrameGraphSource::FrameGraphResourceDecl>& resourceDecls) const;
	

	/* Pass Parameters */
	std::vector<PassParametersSource> GeneratePassesParameterSources(const Parsing::FrameGraphSourceContext& frameGraphContext) const;
	void PostprocessPassesParameterSources(std::vector<PassParametersSource>& parameters) const;
	PassParameters CompilePassParameters(PassParametersSource&& passSource, FrameGraph& frameGraph) const;
	std::vector<PassTask::Callback_t> CompilePassCallbacks(const std::vector<PassParametersSource::FixedFunction_t>& fixedFunctions, const PassParameters& passParams) const;

	/*  Files processing */
	std::unordered_map<std::string, std::string> LoadPassFiles() const;
	std::string	LoadFrameGraphFile() const;

	std::shared_ptr<Parsing::PassParametersContext> ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles,
		const Parsing::FrameGraphSourceContext& frameGraphContext) const;

	std::shared_ptr<Parsing::FrameGraphSourceContext> ParseFrameGraphFile(const std::string& materialFileContent) const;

	/* Shaders */
	std::vector<FrameGraphBuilder::CompiledShaderData> CompileShaders(const PassParametersSource& pass) const;
	
	/* Input layout */
	std::vector<D3D12_INPUT_ELEMENT_DESC> GenerateInputLayout(const PassParametersSource& pass) const;
	const Parsing::VertAttr* GetPassInputVertAttr(const PassParametersSource& pass) const;

	/* State objects processing */
	ComPtr<ID3D12RootSignature> GenerateRootSignature(const PassParametersSource& pass, const std::vector<CompiledShaderData>& shaders) const;
	ComPtr<ID3D12PipelineState>	GeneratePipelineStateObject(
		const PassParametersSource& passSource,
		std::vector<CompiledShaderData>& shaders,
		ComPtr<ID3D12RootSignature>& rootSig) const;

	/* Root arguments */
	void CreateResourceArguments(const PassParametersSource& passSource, FrameGraph& frameGraph, PassParameters& pass) const;
	static void AddRootArg(PassParameters& pass, FrameGraph& frameGraph,
		Parsing::ResourceBindFrequency updateFrequency, Parsing::ResourceScope scope, RootArg::Arg_t&& arg);

	/* Utils */
	void ValidatePassResources(const std::vector<PassParametersSource>& passesParametersSources) const;
	void AttachSpecialPostPreCallbacks(std::vector<PassTask>& passTasks) const;

	HANDLE sourceWatchHandle = INVALID_HANDLE_VALUE;
};