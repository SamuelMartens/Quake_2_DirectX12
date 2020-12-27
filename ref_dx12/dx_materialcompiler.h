#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <memory>

#include "dx_passparameters.h"
#include "dx_material.h"

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
	
	//#DEBUG super IMPORTANT! handle views free
	struct RootSignature
	{
		std::vector<RootParma_t> params;
		std::string rawView;
	};

	struct PassParametersContext
	{
		std::vector<PassParametersSource> passSources;
		std::vector<Parsing::Resource_t> resources;
	};

	struct MaterialContext
	{
		std::vector<std::string> passes;
	};

}

class MaterialCompiler
{

private:
	MaterialCompiler();

public:

	MaterialCompiler(const MaterialCompiler&) = delete;
	MaterialCompiler& operator=(const MaterialCompiler&) = delete;
	MaterialCompiler(MaterialCompiler&&) = delete;
	MaterialCompiler& operator=(MaterialCompiler&&) = delete;

	~MaterialCompiler() = default;

	static MaterialCompiler& Inst();

	using PassCompiledShaders_t = std::vector<std::pair<PassParametersSource::ShaderType, ComPtr<ID3DBlob>>>;

public:

	PassMaterial GenerateMaterial();

	std::filesystem::path GenPathToFile(const std::string fileName) const;

private:

	std::vector<PassParameters> GeneratePasses() const;

	std::unordered_map<std::string, std::string> LoadPassFiles() const;
	std::string	LoadMaterialFile() const;

	

	void PreprocessPassFiles(const std::vector<std::string>& fileList);
	
	std::shared_ptr<Parsing::PassParametersContext> ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles) const;
	std::shared_ptr<Parsing::MaterialContext> ParseMaterialFile(const std::string& materialFileContent) const;

	PassCompiledShaders_t CompileShaders(const PassParametersSource& pass, const std::vector<Parsing::Resource_t>& globalRes) const;
	std::vector<D3D12_INPUT_ELEMENT_DESC> GenerateInputLayout(const PassParametersSource& pass) const;
	const Parsing::VertAttr& GetPassInputVertAttr(const PassParametersSource& pass) const;

	ComPtr<ID3D12RootSignature> GenerateRootSignature(const PassParametersSource& pass, const PassCompiledShaders_t& shaders) const;
	ComPtr<ID3D12PipelineState>	GeneratePipelineStateObject(
		const PassParametersSource& passSource,
		PassCompiledShaders_t& shaders,
		ComPtr<ID3D12RootSignature>& rootSig) const;
	void CreateResourceArguments(const PassParametersSource& passSource, const std::vector<Parsing::Resource_t>& globalRes, PassParameters& pass) const;

	PassParameters CompilePass(const PassParametersSource& passSource, const std::vector<Parsing::Resource_t>& globalRes) const;


	std::filesystem::path ROOT_DIR_PATH;

};