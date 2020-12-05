#include "dx_materialcompiler.h"

#include <string_view>
#include <fstream>
#include <cassert>
#include <vector>
#include <tuple>
#include <d3dcompiler.h>
//#DEBUG
#include <any>
//END

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include "Lib/crc32.h"
#include "Lib/peglib.h"
#include "dx_settings.h"
#include "dx_diagnostics.h"
#include "dx_app.h"

namespace
{

	std::string ReadFile(const std::filesystem::path& filePath)
	{
		std::ifstream file(filePath);

		assert(file.is_open() && "Failed to read the file. File can't be open");

		// Get content of the file
		file.seekg(0, std::ios::end);
		size_t size = file.tellg();
		std::string fileContent(size, ' ');
		file.seekg(0);
		file.read(&fileContent[0], size);

		return fileContent;
	}

	template<typename T>
	void ValidateResource(const T& resource, PassSource::ResourceScope scope, const ParseContext& parseCtx)
	{
#ifdef _DEBUG

		std::string_view resourceName = PassSource::_GetResourceName(resource);

		switch (scope)
		{
		case PassSource::ResourceScope::Local:
		{
			// Local resource can't collide with local resources in the same pass
			// or with any global resource
			const PassSource& currentPassSource = parseCtx.passSources.back();

			// Check against same pass resources
			assert(std::find_if(currentPassSource.resources.cbegin(), currentPassSource.resources.cend(),
				[resourceName](const Resource_t& existingRes) 
			{
				return resourceName == PassSource::GetResourceName(existingRes);

			}) == currentPassSource.resources.cend() &&
				"Local to local resource name collision");

			// Check against other globals
			assert(std::find_if(parseCtx.resources.cbegin(), parseCtx.resources.cend(),
				[resourceName](const Resource_t& existingRes)
			{
				return  resourceName == PassSource::GetResourceName(existingRes);

			}) == parseCtx.resources.cend() &&
				"Global to local resource name collision, local insertion");

			break;
		}
		case PassSource::ResourceScope::Global:
		{
			// Global resource can't collide with other global resource and with any local resource

			for(const PassSource& currentPassSource : parseCtx.passSources)
			{
				// Check against same pass resources
				assert(std::find_if(currentPassSource.resources.cbegin(), currentPassSource.resources.cend(),
					[resourceName](const Resource_t& existingRes)
				{
					return resourceName == PassSource::GetResourceName(existingRes);

				}) == currentPassSource.resources.cend() &&
					"Global to local resource name collision, global insertion");
			}

			// Check against other globals
			assert(std::find_if(parseCtx.resources.cbegin(), parseCtx.resources.cend(),
				[resourceName](const Resource_t& existingRes)
			{
				return resourceName == PassSource::GetResourceName(existingRes);

			}) == parseCtx.resources.cend() &&
				"Global to global resource name collision");

			break;
		}	
		default:
			assert(false && "Can't validate resource. Invalid scope");
			break;
		}
#endif // _DEBUG
	}

	void InitParser(peg::parser& parser) 
	{
		// Load grammar
		const std::string passGrammar = ReadFile(MaterialCompiler::Inst().GenPathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_PASS_FILENAME));

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
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			parseCtx.passSources.back().input = static_cast<PassSource::InputType>(sv.choice());
		};

		parser["PassVertAttr"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			parseCtx.passSources.back().inputVertAttr = peg::any_cast<std::string>(sv[0]);
		};

		parser["PassVertAttrSlots"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			parseCtx.passSources.back().vertAttrSlots = std::move(std::any_cast<std::vector<std::tuple<unsigned int, int>>>(sv[0]));
		};

		// --- State
		parser["ColorTargetSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			parseCtx.passSources.back().colorTargetName = peg::any_cast<std::string>(sv[0]);
		};

		parser["DepthTargetSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			parseCtx.passSources.back().depthTargetName = peg::any_cast<std::string>(sv[0]);
		};

		parser["ViewportSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

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
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.BlendState.RenderTarget[0].BlendEnable = peg::any_cast<bool>(sv[0]);
		};

		parser["SrcBlendAlphaSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = peg::any_cast<D3D12_BLEND>(sv[0]);
		};

		parser["DestBlendAlphaSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = peg::any_cast<D3D12_BLEND>(sv[0]);
		};

		parser["TopologySt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			currentPass.primitiveTopology = peg::any_cast<D3D_PRIMITIVE_TOPOLOGY>(sv[0]);
		};

		parser["DepthWriteMaskSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.DepthStencilState.DepthWriteMask = peg::any_cast<bool>(sv) ?
				D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		};

		parser["BlendStValues"] = [](const peg::SemanticValues& sv)
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
		};

		parser["TopologyStValues"] = [](const peg::SemanticValues& sv) 
		{
			switch (sv.choice())
			{
			case 0:
				return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case 1:
				return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			default:
				assert(false && "Invalid topology state");
				break;
			}

			return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		};

		// --- Root Signatures

		parser["RSig"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			std::string& rootSig = parseCtx.passSources.back().rootSignature;
			
			rootSig = sv.token(); 
			// Later root signature is inserted into shader source code. It must be in one line
			// otherwise shader wouldn't compile
			rootSig.erase(std::remove(rootSig.begin(), rootSig.end(), '\n'), rootSig.end());
			
		};

		// --- Shader code

		parser["ShaderExternalDecl"] = [](const peg::SemanticValues& sv)
		{
			std::vector<std::string> externalList;

			for (int i = 0; i < sv.size(); i += 2)
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
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			PassSource::ShaderSource& shaderSource = currentPass.shaders.emplace_back(PassSource::ShaderSource());

			shaderSource.type = peg::any_cast<PassSource::ShaderType>(sv[0]);
			shaderSource.externals = std::move(peg::any_cast<std::vector<std::string>>(sv[1]));
			shaderSource.source = std::move(peg::any_cast<std::string>(sv[2]));
		};

		parser["ShaderType"] = [](const peg::SemanticValues& sv)
		{
			assert(sv.choice() < PassSource::ShaderType::SIZE && "Error during parsing shader type");

			return static_cast<PassSource::ShaderType>(sv.choice());
		};

		parser["ShaderTypeDecl"] = [](const peg::SemanticValues& sv)
		{
			return peg::any_cast<PassSource::ShaderType>(sv[0]);
		};

		// --- Resources
		parser["VertAttr"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			
			parseCtx.passSources.back().vertAttr.emplace_back(Resource_VertAttr{
				peg::any_cast<std::string>(sv[0]),
				std::move(peg::any_cast<std::vector<VertAttrField>>(sv[1])),
				sv.str()
				});
		};

		parser["Resource"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			//#DEBUG ignore ResourceUpdate for now. Handle Scope only. Do debug validation
			std::tuple<PassSource::ResourceScope, PassSource::ResourceUpdate> resourceAttr =
				peg::any_cast<std::tuple<PassSource::ResourceScope, PassSource::ResourceUpdate>>(sv[0]);

			ParseContext& parseCtx = *std::any_cast<std::shared_ptr<ParseContext>&>(ctx);
			PassSource& currentPass = parseCtx.passSources.back();

			switch (std::get<PassSource::ResourceScope>(resourceAttr))
			{
			case PassSource::ResourceScope::Local:
			{
				if (sv[1].type() == typeid(Resource_ConstBuff))
				{
					ValidateResource(std::any_cast<Resource_ConstBuff>(sv[1]), PassSource::ResourceScope::Local, parseCtx);
					currentPass.resources.emplace_back(std::any_cast<Resource_ConstBuff>(sv[1]));
				}
				else if (sv[1].type() == typeid(Resource_Texture))
				{
					ValidateResource(std::any_cast<Resource_Texture>(sv[1]), PassSource::ResourceScope::Local, parseCtx);
					currentPass.resources.emplace_back(std::any_cast<Resource_Texture>(sv[1]));
				}
				else if (sv[1].type() == typeid(Resource_Sampler))
				{
					ValidateResource(std::any_cast<Resource_Sampler>(sv[1]), PassSource::ResourceScope::Local, parseCtx);
					currentPass.resources.emplace_back(std::any_cast<Resource_Sampler>(sv[1]));
				}
				else
				{
					assert(false && "Resource callback invalid type");
				}
				break;
			}
			case PassSource::ResourceScope::Global:
			{
				if (sv[1].type() == typeid(Resource_ConstBuff))
				{
					ValidateResource(std::any_cast<Resource_ConstBuff>(sv[1]), PassSource::ResourceScope::Global, parseCtx);
					parseCtx.resources.emplace_back(std::any_cast<Resource_ConstBuff>(sv[1]));
				}
				else if (sv[1].type() == typeid(Resource_Texture))
				{
					ValidateResource(std::any_cast<Resource_Texture>(sv[1]), PassSource::ResourceScope::Global, parseCtx);
					parseCtx.resources.emplace_back(std::any_cast<Resource_Texture>(sv[1]));
				}
				else if (sv[1].type() == typeid(Resource_Sampler))
				{
					ValidateResource(std::any_cast<Resource_Sampler>(sv[1]), PassSource::ResourceScope::Global, parseCtx);
					parseCtx.resources.emplace_back(std::any_cast<Resource_Sampler>(sv[1]));
				}
				else
				{
					assert(false && "Resource callback invalid type");
				}
				break;
			}
			default:
				assert(false && "Resource callback undefined resource scope type");
				break;
			}

		};

		parser["ConstBuff"] = [](const peg::SemanticValues& sv)
		{
			return Resource_ConstBuff{
				peg::any_cast<std::string>(sv[0]),
				peg::any_cast<std::string>(sv[1]),
				peg::any_cast<std::string>(sv[2]),
				sv.str()};
		};

		parser["Texture"] = [](const peg::SemanticValues& sv)
		{
			return Resource_Texture{
				peg::any_cast<std::string>(sv[0]),
				peg::any_cast<std::string>(sv[1]),
				sv.str()};
		};


		parser["Sampler"] = [](const peg::SemanticValues& sv)
		{
			return Resource_Sampler{
				peg::any_cast<std::string>(sv[0]),
				peg::any_cast<std::string>(sv[1]),
				sv.str()};
		};

		parser["ResourceAttr"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(
				peg::any_cast<PassSource::ResourceScope>(sv[0]),
				peg::any_cast<PassSource::ResourceUpdate>(sv[1]));
		};

		parser["ResourceScope"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<PassSource::ResourceScope>(sv.choice());
		};

		parser["ResourceUpdate"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<PassSource::ResourceUpdate>(sv.choice());
		};

		parser["VertAttrContent"] = [](const peg::SemanticValues& sv)
		{
			std::vector<VertAttrField> content;

			std::transform(sv.begin(), sv.end(), std::back_inserter(content),
				[](const peg::any& field) { return std::any_cast<VertAttrField>(field); });

			return content;
		};

		parser["VertAttrField"] = [](const peg::SemanticValues& sv) 
		{
			std::string name = peg::any_cast<std::string>(sv[1]);
			std::tuple<std::string, unsigned int> semanticInfo = 
				peg::any_cast<std::tuple<std::string, unsigned int>>(sv[2]);

			return VertAttrField{
				peg::any_cast<ParseDataType>(sv[0]),
				HASH(name.c_str()),
				std::get<std::string>(semanticInfo),
				std::get<unsigned int>(semanticInfo),
				std::move(name)
			};
		};

		parser["VertAttrFieldSlot"] = [](const peg::SemanticValues& sv) 
		{
			std::vector<std::tuple<unsigned int, int>> result;

			for (int i = 0; i < sv.size(); i += 2)
			{
				result.push_back(peg::any_cast<std::tuple<unsigned int, int>>(sv[i]));
			}

			return result;
		};

		parser["VertAttrFieldSlot"] = [](const peg::SemanticValues& sv) 
		{
			return std::make_tuple(HASH(peg::any_cast<std::string>(sv[0]).c_str()), peg::any_cast<int>(sv[1]));
		};

		parser["ResourceFieldType"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<ParseDataType>(sv.choice());
		};

		parser["ResourceFieldSemantic"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(peg::any_cast<std::string>(sv[0]), 
				sv.size() > 1 ? static_cast<unsigned int>(peg::any_cast<int>(sv[1])) : 0);
		};

		// --- Tokens
		parser["Ident"] = [](const peg::SemanticValues& sv)
		{
			return sv.token();
		};

		parser["RegisterDecl"] = [](const peg::SemanticValues& sv)
		{
			return peg::any_cast<std::string>(sv[0]);
		};

		parser["RegisterId"] = [](const peg::SemanticValues& sv) 
		{
			return sv.str();
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

		parser["Word"] = [](const peg::SemanticValues& sv)
		{
			return sv.token();
		};
	};

	using PassCompiledShaders_t = std::vector<std::pair<PassSource::ShaderType, ComPtr<ID3DBlob>>>;

	std::unordered_map<std::string, PassCompiledShaders_t> CompileShaders(ParseContext& ctx)
	{
		std::unordered_map<std::string, PassCompiledShaders_t> compiledShaders;

		for (PassSource& pass : ctx.passSources)
		{

			PassCompiledShaders_t passCompiledShaders;
			
			for (PassSource::ShaderSource& shader : pass.shaders)
			{
				std::string resToInclude;

				// Add External Resources
				for (std::string& externalRes : shader.externals)
				{
					// Find resource and stub it into shader source
					
					// Search in local scope first
					const auto localResIt = std::find_if(pass.resources.cbegin(), pass.resources.cend(), 
						[externalRes](const Resource_t& currRes)
					{
						return externalRes == PassSource::GetResourceName(currRes);
					});

					if (localResIt != pass.resources.cend())
					{
						resToInclude += PassSource::GetResourceRawView(*localResIt);
						resToInclude += ";";
					}
					else
					{
						// Search in global scope
						const auto globalResIt = std::find_if(ctx.resources.cbegin(), ctx.resources.cend(),
							[externalRes](const Resource_t& currRes)
						{
							return externalRes == PassSource::GetResourceName(currRes);
						});

						if (globalResIt != ctx.resources.cend())
						{
							resToInclude += PassSource::GetResourceRawView(*globalResIt);
							resToInclude += ";";
						}
						else
						{
							// Finally try vert attributes
							const auto vertAttrIt = std::find_if(pass.vertAttr.cbegin(), pass.vertAttr.cend(),
								[externalRes](const Resource_VertAttr& currVert)
							{
								return externalRes == currVert.name;
							});

							assert(vertAttrIt != pass.vertAttr.cend() && "External resource can't be found");

							resToInclude += vertAttrIt->rawView;
							resToInclude += ";";
						}
					}
				}

				shader.source = 
					resToInclude + 
					"[RootSignature( \" " + pass.rootSignature + " \" )]" +
					shader.source;

				// Got final shader source, now compile
				ComPtr<ID3DBlob>& shaderBlob = passCompiledShaders.emplace_back(std::make_pair(shader.type, ComPtr<ID3DBlob>())).second;
				ComPtr<ID3DBlob> errors;

				const std::string strShaderType = PassSource::ShaderTypeToStr(shader.type);

				HRESULT hr = D3DCompile(
					shader.source.c_str(),
					shader.source.size(),
					(pass.name + strShaderType).c_str(),
					nullptr,
					nullptr,
					"main",
					(Utils::StrToLower(strShaderType) + "_5_1").c_str(),
					Settings::SHADER_COMPILATION_FLAGS,
					0,
					&shaderBlob,
					&errors
				);

				if (errors != nullptr)
				{
					Logs::Logf(Logs::Category::Parser, "Shader compilation error: %s", 
						reinterpret_cast<char*>(errors->GetBufferPointer()));
				}

				ThrowIfFailed(hr);
			}

			compiledShaders[pass.name] = std::move(passCompiledShaders);
		}

		return compiledShaders;
	}
}


MaterialCompiler::MaterialCompiler()
{
	std::string pathToThisFile = __FILE__;
	ROOT_DIR_PATH = pathToThisFile.substr(0, pathToThisFile.rfind("\\"));
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
	std::shared_ptr<ParseContext> parseCtx = ParsePassFiles(LoadPassFiles());

	CompileShaders(*parseCtx);

	//#DEBUG
	for (const PassSource& passSource : parseCtx->passSources)
	{
		GenerateInputLayout(passSource);
	}
	//END
}

std::unordered_map<std::string, std::string> MaterialCompiler::LoadPassFiles()
{
	std::unordered_map<std::string, std::string> passFiles;

	for (const auto& file : std::filesystem::directory_iterator(GenPathToFile(Settings::MATERIAL_DIR)))
	{
		const std::filesystem::path filePath = file.path();

		if (filePath.extension() == Settings::MATERIAL_PASS_FILE_EXT)
		{
			std::string passFileContent = ReadFile(filePath);

			passFiles.emplace(std::make_pair(
				filePath.filename().string(),
				std::move(passFileContent)));
		}
	}

	return passFiles;
}

void MaterialCompiler::PreprocessPassFiles(const std::vector<std::string>& fileList)
{
	//#DEBUG going to make it work without preprocess first. And the add preprocessing
}

//#DEBUG not sure returning context is fine
std::shared_ptr<ParseContext> MaterialCompiler::ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles)
{
	peg::parser parser;
	InitParser(parser);

	std::shared_ptr<ParseContext> context = std::make_shared<ParseContext>();

	for (const auto& passFile : passFiles)
	{
		//#DEBUG
		std::string passName = passFile.first.substr(0, passFile.first.rfind('.'));

		if (passName != "UI")
		{
			continue;
		}
		//END

		context->passSources.emplace_back(PassSource()).name = passFile.first.substr(0, passFile.first.rfind('.'));

		peg::any ctx = context;

		parser.parse(passFile.second.c_str(), ctx);
	}

	return context;
}

std::vector<D3D12_INPUT_ELEMENT_DESC> MaterialCompiler::GenerateInputLayout(const PassSource& pass) const
{

	const std::string& inputName = pass.inputVertAttr;

	const auto attrIt = std::find_if(pass.vertAttr.cbegin(), pass.vertAttr.cend(),
		[inputName](const Resource_VertAttr& attr) { return inputName == attr.name; });

	assert(attrIt != pass.vertAttr.cend() && "Can't find input vert attribute");

	assert((pass.vertAttrSlots.empty() || pass.vertAttrSlots.size() == attrIt->content.size())
		&& "Invalid vert attr slots num, for input layout generation");

	std::array<unsigned int, 16> inputSlotOffset;
	std::fill(inputSlotOffset.begin(), inputSlotOffset.end(), 0);

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	
	for (int i = 0; i < attrIt->content.size(); ++i)
	{
		const VertAttrField& field = attrIt->content[i];

		const auto inputSlotIt = std::find_if(pass.vertAttrSlots.cbegin(), pass.vertAttrSlots.cend(),
			[field](const std::tuple<unsigned int, int>& slot) 
		{
			return	field.hashedName == std::get<0>(slot);
		});

		const int inputSlot = inputSlotIt == pass.vertAttrSlots.cend() ? 0 : std::get<1>(*inputSlotIt);

		inputLayout.push_back(
			{
				field.semanticName.c_str(),
				field.semanticIndex,
				GetParseDataTypeDXGIFormat(field.type),
				static_cast<unsigned>(inputSlot),
				inputSlotOffset[inputSlot],
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				0U
			});

		inputSlotOffset[inputSlot] += GetParseDataTypeSize(field.type);
	}

	return inputLayout;
}

std::filesystem::path MaterialCompiler::GenPathToFile(const std::string fileName) const
{
	std::filesystem::path path = ROOT_DIR_PATH;
	path.append(fileName);

	return path;
}
