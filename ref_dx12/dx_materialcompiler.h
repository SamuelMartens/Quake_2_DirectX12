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


public:
	//#DEBUG making it all public for now, I don't have final design yet
	void GenerateMaterial();
	std::unordered_map<std::string, std::string> LoadPassFiles();
	std::filesystem::path GenPathToFile(const std::string fileName) const;

	void PreprocessPassFiles(const std::vector<std::string>& fileList);
	std::shared_ptr<ParseContext> ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles);

private:

	std::filesystem::path ROOT_DIR_PATH;

};