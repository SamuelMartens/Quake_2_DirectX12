#include "dx_materialcompiler.h"

#include <filesystem>
#include <string_view>
#include <fstream>
#include <cassert>
#include <variant>
#include <vector>
#include <memory>

#include "peglib.h"
#include "dx_settings.h"
#include "dx_diagnostics.h"
#include "dx_material.h"
#include "dx_app.h"

namespace
{
	struct Resource_Attr
	{
		std::string name;
		std::string content;
	};

	struct Resource_ConstBuff
	{
		std::string name;
		std::string registerName;
		std::string content;
	};

	struct Resource_Texture
	{
		std::string name;
		std::string registerName;
	};

	struct Resource_Sampler
	{
		std::string name;
		std::string registerName;
	};

	using  ResourceType_t = std::variant<Resource_Attr, Resource_ConstBuff, Resource_Texture, Resource_Sampler>;

	struct PassParseContext
	{
		std::vector<PassSource> passSource;
		std::vector<ResourceType_t> resources;
	};

	std::string ReadFile(const std::filesystem::path& filePath)
	{
		std::ifstream file(filePath);

		// Get content of the file
		file.seekg(0, std::ios::end);
		size_t size = file.tellg();
		std::string fileContent(size, ' ');
		file.seekg(0);
		file.read(&fileContent[0], size);

		return fileContent;
	}

	D3D12_BLEND ParserBlendToD3D12Blend(const peg::SemanticValues& sv)
	{
		switch (sv.choice())
		{
		case 0:
			return D3D12_BLEND_SRC_ALPHA;
			break;
		case 1:
			return D3D12_BLEND_INV_SRC_ALPHA;
			break;
		default:
			assert(false && "Invalid blend state");
			break;
		}

		return D3D12_BLEND_ZERO;
	}
}


MaterialCompiler::MaterialCompiler()
{

}

MaterialCompiler& MaterialCompiler::Inst()
{
	static MaterialCompiler* matCompiler = nullptr;

	if (matCompiler == nullptr)
	{
		matCompiler = new MaterialCompiler();
	}

	return *matCompiler;
}

void MaterialCompiler::GenerateMaterial()
{
}

std::unordered_map<std::string, std::string> MaterialCompiler::LoadPassFiles()
{
	std::unordered_map<std::string, std::string> passFiles;

	for (const auto& file : std::filesystem::directory_iterator(Settings::MATERIAL_DIR))
	{
		const std::filesystem::path filePath = file.path();

		if (filePath.extension() == Settings::MATERIAL_PASS_FILE_EXT)
		{
			std::string passFileContent = ReadFile(filePath);

			passFiles.emplace(std::make_pair(
				filePath.string(),
				std::move(passFileContent)));
		}
	}

	return passFiles;
}

void MaterialCompiler::PreprocessPassFiles(const std::vector<std::string>& fileList)
{
	//#DEBUG going to make it work without preprocess first. And the add preprocessing
}

void MaterialCompiler::ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles)
{
	//#DEBUG move it to the place where actual parsing will be, i.e. file by file iteration
	peg::any parsingContext = std::make_shared<PassParseContext>();

	// Load grammar
	const std::string passGrammar = ReadFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_PASS_FILENAME);
	
	peg::parser parser;

	parser.log = [](size_t line, size_t col, const std::string& msg)
	{
		Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

		assert(false && "Parsing error");
	};

	bool loadGrammarResult = parser.load_grammar(passGrammar.c_str());
	assert(loadGrammarResult && "Can't load pass grammar");

	// Set up callbacks
	parser["PassInputIdent"] = [](const peg::SemanticValues& sv, peg::any& ctx) 
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		passCtx.passSource.back().input = static_cast<PassSource::InputType>(sv.choice());
	};

	//#DEBUG write resource validation in the end. Make sure not same resources are in

	// --- State
	parser["ColorTargetSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		passCtx.passSource.back().colorTargetName = peg::any_cast<std::string>(sv[0]);
	};

	parser["DepthTargetSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		passCtx.passSource.back().depthTargetName = peg::any_cast<std::string>(sv[0]);
	};

	parser["ViewportSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back();

		// This might be a bit buggy. I am pretty sure that camera viewport is always equal to drawing area
		// but I might not be the case.
		int width, height;
		Renderer::Inst().GetDrawAreaSize(&width, &height);
		
		currentPass.viewport.TopLeftX = sv[0].type() == typeid(int) ?
			peg::any_cast<int>(sv[0]) : peg::any_cast<float>(sv[0]) * width;

		currentPass.viewport.TopLeftY = sv[2].type() == typeid(int) ?
			peg::any_cast<int>(sv[2]) : peg::any_cast<float>(sv[2]) * height;

		currentPass.viewport.Width = sv[4].type() == typeid(int) ?
			peg::any_cast<int>(sv[4]) : peg::any_cast<float>(sv[4]) * width;

		currentPass.viewport.Height = sv[6].type() == typeid(int) ?
			peg::any_cast<int>(sv[6]) : peg::any_cast<float>(sv[6]) * height;
	};

	parser["BlendEnabledSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back(); 

		currentPass.psoDesc.BlendState.RenderTarget[0].BlendEnable = peg::any_cast<bool>(sv[0]);
	};

	parser["SrcBlendAlphaSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back();

		currentPass.psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = ParserBlendToD3D12Blend(sv);
	};

	parser["DestBlendAlphaSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back();

		currentPass.psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = ParserBlendToD3D12Blend(sv);
	};

	parser["TopologySt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back(); 

		switch (sv.choice())
		{
		case 0:
			currentPass.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			break;
		case 1:
			currentPass.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			break;
		default:
			break;
		}
	};

	parser["DepthWriteMaskSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back();

		currentPass.psoDesc.DepthStencilState.DepthWriteMask = peg::any_cast<bool>(sv) ?
			D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	};

	// --- Shader code

	parser["ShaderType"] = [](const peg::SemanticValues& sv)
	{
		assert(sv.choice() < PassSource::ShaderType::SIZE && "Error during parsing shader type");

		return static_cast<PassSource::ShaderType>(sv.choice());
	};

	parser["ShaderExternalDecl"] = [](const peg::SemanticValues& sv) 
	{
		std::vector<std::string> externalList;

		for(int i = 0 ; i < sv.size() ; i+=2)
		{
			externalList.push_back(peg::any_cast<std::string>(sv[i]));
		}

		return externalList;
	};

	parser["ShaderSource"] = [](const peg::SemanticValues& sv)
	{
		return sv.token();
	};

	parser["Shader"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);
		PassSource& currentPass = passCtx.passSource.back();

		PassSource::ShaderSource& shaderSource = currentPass.shaders.emplace_back(PassSource::ShaderSource());

		shaderSource.type = peg::any_cast<PassSource::ShaderType>(sv[0]);
		shaderSource.externalList = std::move(peg::any_cast<std::vector<std::string>>(sv[2]));
		shaderSource.source = std::move(peg::any_cast<std::string>(sv[3]));
	};

	// --- Resources
	parser["Attr"] = [](const peg::SemanticValues& sv, peg::any& ctx) 
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);

		passCtx.resources.emplace_back(Resource_Attr{ 
			peg::any_cast<std::string>(sv[0]),
			peg::any_cast<std::string>(sv[1]) });
	};

	parser["ConstBuff"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);

		passCtx.resources.emplace_back(Resource_ConstBuff{ 
			peg::any_cast<std::string>(sv[0]),
			peg::any_cast<std::string>(sv[1]),
			peg::any_cast<std::string>(sv[1]) });
	};

	parser["Texture"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);

		passCtx.resources.emplace_back(Resource_Texture{
			peg::any_cast<std::string>(sv[0]),
			peg::any_cast<std::string>(sv[1]) });
	};


	parser["Sampler"] = [](const peg::SemanticValues& sv, peg::any& ctx)
	{
		PassParseContext& passCtx = *std::any_cast<std::shared_ptr<PassParseContext>&>(ctx);

		passCtx.resources.emplace_back(Resource_Sampler{
			peg::any_cast<std::string>(sv[0]),
			peg::any_cast<std::string>(sv[1]) });
	};

	// --- Tokens
	parser["Ident"] = [](const peg::SemanticValues& sv) 
	{
		return sv.token();
	};

	parser["RegisterIdent"] = [](const peg::SemanticValues& sv)
	{
		return sv.token();
	};

	parser["ResourceContent"] = [](const peg::SemanticValues& sv)
	{
		return sv.token();
	};

	// -- Types
	parser["Bool"] = [](const peg::SemanticValues& sv)
	{
		return sv.choice() == 0;
	};

	parser["Float"] = [](const peg::SemanticValues& sv) 
	{
		return stof(sv.token());
	};

	parser["Int"] = [](const peg::SemanticValues& sv)
	{
		return stoi(sv.token());
	};
}
