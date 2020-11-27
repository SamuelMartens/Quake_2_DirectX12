#pragma once

#include <vector>
#include <string>
#include <unordered_map>

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

	void PreprocessPassFiles(const std::vector<std::string>& fileList);
	void ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles);

};