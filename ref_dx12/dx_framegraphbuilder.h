#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <memory>

#include "dx_passparameters.h"
#include "dx_material.h"
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

	struct PassParametersContext
	{
		std::vector<PassParametersSource> passSources;
		//#DEBUG remove this
		std::vector<Parsing::Resource_t> resources;
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

	FrameGraph BuildFrameGraph();

	std::filesystem::path GenPathToFile(const std::string fileName) const;

private:

	FrameGraph CompileFrameGraph(FrameGraphSource&& source) const;
	FrameGraphSource GenerateFrameGraphSource() const;
	
	std::vector<PassParametersSource> GeneratePassesParameterSources() const;

	std::unordered_map<std::string, std::string> LoadPassFiles() const;
	std::string	LoadFrameGraphFile() const;

	void PreprocessPassFiles(const std::vector<std::string>& fileList);
	
	std::shared_ptr<Parsing::PassParametersContext> ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles) const;
	std::shared_ptr<Parsing::FrameGraphSourceContext> ParseFrameGraphFile(const std::string& materialFileContent) const;

	PassCompiledShaders_t CompileShaders(const PassParametersSource& pass) const;
	std::vector<D3D12_INPUT_ELEMENT_DESC> GenerateInputLayout(const PassParametersSource& pass) const;
	const Parsing::VertAttr& GetPassInputVertAttr(const PassParametersSource& pass) const;

	ComPtr<ID3D12RootSignature> GenerateRootSignature(const PassParametersSource& pass, const PassCompiledShaders_t& shaders) const;
	ComPtr<ID3D12PipelineState>	GeneratePipelineStateObject(
		const PassParametersSource& passSource,
		PassCompiledShaders_t& shaders,
		ComPtr<ID3D12RootSignature>& rootSig) const;
	void CreateResourceArguments(const PassParametersSource& passSource, FrameGraph& frameGraph, PassParameters& pass) const;

	PassParameters CompilePassParameters(PassParametersSource&& passSource, FrameGraph& frameGraph) const;

	void InitPass(PassParameters&& passParameters, Pass_t& pass) const;

	std::filesystem::path ROOT_DIR_PATH;

};