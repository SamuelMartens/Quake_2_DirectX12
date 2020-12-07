#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <memory>

#include "dx_material.h"

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

	using PassCompiledShaders_t = std::vector<std::pair<PassSource::ShaderType, ComPtr<ID3DBlob>>>;

public:


	//#DEBUG making it all public for now, I don't have final design yet
	PassMaterial GenerateMaterial();
	std::vector<Pass> GeneratePasses() const;

	std::unordered_map<std::string, std::string> LoadPassFiles() const;
	std::string	LoadMaterialFile() const;

	std::filesystem::path GenPathToFile(const std::string fileName) const;

	void PreprocessPassFiles(const std::vector<std::string>& fileList);
	
	std::shared_ptr<ParsePassContext> ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles) const;
	std::shared_ptr<ParseMaterialContext> ParseMaterialFile(const std::string& materialFileContent) const;

	PassCompiledShaders_t CompileShaders(const PassSource& pass, const std::vector<Resource_t>& globalRes) const;
	std::vector<D3D12_INPUT_ELEMENT_DESC> GenerateInputLayout(const PassSource& pass) const;
	ComPtr<ID3D12RootSignature> GenerateRootSignature(const PassSource& pass, const PassCompiledShaders_t& shaders) const;
	ComPtr<ID3D12PipelineState>	GeneratePipelineStateObject(
		const PassSource& passSource,
		PassCompiledShaders_t& shaders,
		ComPtr<ID3D12RootSignature>& rootSig) const;

	Pass CompilePass(const PassSource& passSource, const std::vector<Resource_t>& globalRes) const;


private:


	std::filesystem::path ROOT_DIR_PATH;

};