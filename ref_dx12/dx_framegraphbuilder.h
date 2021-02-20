#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <memory>

#include "dx_passparameters.h"
#include "dx_framegraph.h"

namespace Parsing
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

	// All these views can be used for both inline descriptor declaration and
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
	
	struct RootSignature
	{
		std::vector<RootParma_t> params;
		std::string rawView;
	};

	struct PreprocessorContext
	{
		struct Include
		{
			std::string name;
			int pos = Const::INVALID_OFFSET;
			int len = Const::INVALID_SIZE;
		};

		std::unordered_map<std::string, std::vector<Include>> includes;

		std::string currentFile;
	};

	struct PassParametersContext
	{
		std::vector<PassParametersSource> passSources;
	};

	struct FrameGraphSourceContext
	{
		std::vector<std::string> passes;
	};

}

class FrameGraphBuilder
{

private:
	FrameGraphBuilder();

public:

	FrameGraphBuilder(const FrameGraphBuilder&) = delete;
	FrameGraphBuilder& operator=(const FrameGraphBuilder&) = delete;
	FrameGraphBuilder(FrameGraphBuilder&&) = delete;
	FrameGraphBuilder& operator=(FrameGraphBuilder&&) = delete;

	~FrameGraphBuilder() = default;

	static FrameGraphBuilder& Inst();

	using PassCompiledShaders_t = std::vector<std::pair<PassParametersSource::ShaderType, ComPtr<ID3DBlob>>>;

public:

	bool BuildFrameGraph(std::unique_ptr<FrameGraph>& outFrameGraph);

	std::filesystem::path GenPathToFile(const std::string fileName) const;

private:

	/* FrameGraph generation */
	FrameGraphSource GenerateFrameGraphSource() const;

	[[nodiscard]]
	FrameGraph CompileFrameGraph(FrameGraphSource&& source) const;
	
	/* Pass Parameters */
	std::vector<PassParametersSource> GeneratePassesParameterSources() const;
	PassParameters CompilePassParameters(PassParametersSource&& passSource, FrameGraph& frameGraph) const;

	/*  Files processing */
	std::unordered_map<std::string, std::string> LoadPassFiles() const;
	std::string	LoadFrameGraphFile() const;
	
	std::shared_ptr<Parsing::PreprocessorContext> ParsePreprocessPassFiles(const std::unordered_map<std::string, std::string>& passFiles) const;

	std::shared_ptr<Parsing::PassParametersContext> ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles) const;
	std::shared_ptr<Parsing::FrameGraphSourceContext> ParseFrameGraphFile(const std::string& materialFileContent) const;

	bool IsSourceChanged();

	void PreprocessPassFiles(std::unordered_map<std::string, std::string>& passFiles, Parsing::PreprocessorContext& context) const;

	/* Shaders */
	PassCompiledShaders_t CompileShaders(const PassParametersSource& pass) const;
	
	/* Input layout */
	std::vector<D3D12_INPUT_ELEMENT_DESC> GenerateInputLayout(const PassParametersSource& pass) const;
	const Parsing::VertAttr& GetPassInputVertAttr(const PassParametersSource& pass) const;

	/* State objects processing */
	ComPtr<ID3D12RootSignature> GenerateRootSignature(const PassParametersSource& pass, const PassCompiledShaders_t& shaders) const;
	ComPtr<ID3D12PipelineState>	GeneratePipelineStateObject(
		const PassParametersSource& passSource,
		PassCompiledShaders_t& shaders,
		ComPtr<ID3D12RootSignature>& rootSig) const;

	/*  Root arguments */
	void CreateResourceArguments(const PassParametersSource& passSource, FrameGraph& frameGraph, PassParameters& pass) const;
	static void AddRootArg(PassParameters& pass, FrameGraph& frameGraph,
		Parsing::ResourceBindFrequency updateFrequency, Parsing::ResourceScope scope, RootArg::Arg_t&& arg);

	/* Utils */
	void ValidateResources(const std::vector<PassParametersSource>& passesParametersSources) const;

	std::filesystem::path ROOT_DIR_PATH;
	HANDLE sourceWatchHandle = INVALID_HANDLE_VALUE;
};