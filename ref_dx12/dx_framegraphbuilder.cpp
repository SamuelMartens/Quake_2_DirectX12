#include "dx_framegraphbuilder.h"

#include <string_view>
#include <fstream>
#include <cassert>
#include <vector>
#include <tuple>
#include <d3dcompiler.h>
#include <optional>


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
#include "dx_infrastructure.h"
#include "dx_memorymanager.h"


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
	const T* FindResourceOfTypeAndRegId(const std::vector<Parsing::Resource_t>& resources, int registerId)
	{
		for (const Parsing::Resource_t& res : resources)
		{
			// Visit can't return different types for different invocations. So we just use it to find right resource
			const bool isTargetRes = std::visit([registerId](auto&& res)
			{
				using resT = std::decay_t<decltype(res)>;

				if constexpr (std::is_same_v<T, resT>)
				{
					return res.registerId == registerId;
				}

				return false;

			}, res);

			if (isTargetRes)
			{
				return &std::get<T>(res);
			}
		}

		return nullptr;
	}

	void SetResourceBindFrequency(Parsing::Resource_t& r, Parsing::ResourceBindFrequency bind)
	{
		std::visit([bind](auto&& resource) 
		{
			resource.bindFrequency = bind;
		}, r);
	}

	void SetResourceScope(Parsing::Resource_t& r, Parsing::ResourceScope scope)
	{
		std::visit([scope](auto&& resource) 
		{
			resource.scope = scope;
		}, r);
	}

	void InitPassParser(peg::parser& parser) 
	{
		// Load grammar
		const std::string passGrammar = ReadFile(FrameGraphBuilder::Inst().GenPathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_PASS_FILENAME));

		parser.log = [](size_t line, size_t col, const std::string& msg)
		{
			Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

			assert(false && "Parsing error");
		};

		const bool loadGrammarResult = parser.load_grammar(passGrammar.c_str());
		assert(loadGrammarResult && "Can't load pass grammar");

		// Set up callbacks

		// --- Top level pass tokens
		parser["PassInputIdent"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().input = static_cast<Parsing::PassInputType>(sv.choice());
		};

		parser["PassVertAttr"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().inputVertAttr = peg::any_cast<std::string>(sv[0]);
		};

		parser["PassVertAttrSlots"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().vertAttrSlots = std::move(std::any_cast<std::vector<std::tuple<unsigned int, int>>>(sv[0]));
		};

		// --- State
		parser["ColorTargetSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().colorTargetName = peg::any_cast<std::string>(sv[0]);
		};

		parser["DepthTargetSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().depthTargetName = peg::any_cast<std::string>(sv[0]);
		};

		parser["ViewportSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

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
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.BlendState.RenderTarget[0].BlendEnable = peg::any_cast<bool>(sv[0]);
		};

		parser["SrcBlendAlphaSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = peg::any_cast<D3D12_BLEND>(sv[0]);
		};

		parser["DestBlendAlphaSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = peg::any_cast<D3D12_BLEND>(sv[0]);
		};

		parser["TopologySt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			auto topology = peg::any_cast<std::tuple<D3D_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE>>(sv[0]);

			currentPass.primitiveTopology = std::get<D3D_PRIMITIVE_TOPOLOGY>(topology);
			currentPass.psoDesc.PrimitiveTopologyType = std::get<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(topology);
		};

		parser["DepthWriteMaskSt"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

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
				return std::make_tuple(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
			case 1:
				return std::make_tuple(D3D_PRIMITIVE_TOPOLOGY_POINTLIST, D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
			default:
				assert(false && "Invalid topology state");
				break;
			}

			return std::make_tuple(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
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
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			PassParametersSource::ShaderSource& shaderSource = currentPass.shaders.emplace_back(PassParametersSource::ShaderSource());

			shaderSource.type = peg::any_cast<PassParametersSource::ShaderType>(sv[0]);
			shaderSource.externals = std::move(peg::any_cast<std::vector<std::string>>(sv[1]));
			shaderSource.source = std::move(peg::any_cast<std::string>(sv[2]));
		};

		parser["ShaderType"] = [](const peg::SemanticValues& sv)
		{
			assert(sv.choice() < PassParametersSource::ShaderType::SIZE && "Error during parsing shader type");

			return static_cast<PassParametersSource::ShaderType>(sv.choice());
		};

		parser["ShaderTypeDecl"] = [](const peg::SemanticValues& sv)
		{
			return peg::any_cast<PassParametersSource::ShaderType>(sv[0]);
		};

		// --- Root Signature
		parser["RSig"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			Parsing::RootSignature& rootSig = *parseCtx.passSources.back().rootSignature;

			rootSig.rawView = sv.token();
			// Later root signature is inserted into shader source code. It must be in one line
			// otherwise shader wouldn't compile
			rootSig.rawView.erase(std::remove(rootSig.rawView.begin(), rootSig.rawView.end(), '\n'), rootSig.rawView.end());

			std::for_each(sv.begin() + 1, sv.end(),
				[&rootSig](const peg::any& token) 
			{
				if (token.type() == typeid(Parsing::RootParam_ConstBuffView))
				{
					Parsing::RootParam_ConstBuffView cbv = peg::any_cast<Parsing::RootParam_ConstBuffView>(token);
					assert(cbv.num == 1 && "CBV Inline descriptor can't have more that 1 num");

					rootSig.params.push_back(std::move(cbv));
				}
				else if (token.type() == typeid(Parsing::RootParam_DescTable))
				{
					Parsing::RootParam_DescTable descTable = peg::any_cast<Parsing::RootParam_DescTable>(token);
					rootSig.params.push_back(std::move(descTable));
				}
				else 
				{
					assert(false && "Invalid root parameter");
				};
			});
		};


		parser["RSigStatSamplerDecl"] = [](const peg::SemanticValues& sv)
		{
			assert(false && "Static samplers are not implemented");
		};

		parser["RSigRootConstDecl"] = [](const peg::SemanticValues& sv) 
		{
			assert(false && "Root constants are not implemented");
		};

		parser["RSigDescTableDecl"] = [](const peg::SemanticValues& sv)
		{
			Parsing::RootParam_DescTable descTable;

			std::for_each(sv.begin(), sv.end(),
				[&descTable](const peg::any& token)
			{
				if (token.type() == typeid(Parsing::RootParam_TextView))
				{
					descTable.entities.push_back(peg::any_cast<Parsing::RootParam_TextView>(token));
				}
				else if (token.type() == typeid(Parsing::RootParam_ConstBuffView))
				{
					descTable.entities.push_back(peg::any_cast<Parsing::RootParam_ConstBuffView>(token));
				}
				else if (token.type() == typeid(Parsing::RootParam_SamplerView))
				{
					descTable.entities.push_back(peg::any_cast<Parsing::RootParam_SamplerView>(token));
				}
				else
				{
					assert(false && "Unknown type for desc table entity");
				}
			});

			return descTable;
		};

		parser["RSigCBVDecl"] = [](const peg::SemanticValues& sv)
		{
			int num = 1;

			for (int i = 2 ; i < sv.size(); i += 2)
			{
				auto option = peg::any_cast<std::tuple<Parsing::Option, int>>(sv[i]);

				switch (std::get<Parsing::Option>(option))
				{
				case Parsing::Option::NumDecl:
					num = std::get<int>(option);
					break;
				case Parsing::Option::Visibility:
					break;
				default:
					assert(false && "Invalid root param option in CBV decl");
					break;
				}
			}

			return Parsing::RootParam_ConstBuffView{
				peg::any_cast<int>(sv[0]),
				num
			};
		};

		parser["RSigSRVDecl"] = [](const peg::SemanticValues& sv)
		{
			int num = 1;

			for (int i = 2; i < sv.size(); i += 2)
			{
				auto option = peg::any_cast<std::tuple<Parsing::Option, int>>(sv[i]);

				switch (std::get<Parsing::Option>(option))
				{
				case Parsing::Option::NumDecl:
					num = std::get<int>(option);
					break;
				case Parsing::Option::Visibility:
					break;
				default:
					assert(false && "Invalid root param option in SRV decl");
					break;
				}
			}

			return Parsing::RootParam_TextView{
				peg::any_cast<int>(sv[0]),
				num
			};
		};

		parser["RSigUAVDecl"] = [](const peg::SemanticValues& sv) 
		{
			assert(false && "UAV is not implemented");
		};

		parser["RSigDescTableSampler"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::RootParam_SamplerView{
				peg::any_cast<int>(sv[0]),
				sv.size() == 1 ? 1 :  peg::any_cast<int>(sv[2])
			};
		};

		parser["RSigDeclOptions"] = [](const peg::SemanticValues& sv)
		{
			switch (sv.choice())
			{
			case 0:
				return std::make_tuple(Parsing::Option::Visibility, 0);
			case 1:
				return std::make_tuple(Parsing::Option::NumDecl, peg::any_cast<int>(sv[0]));
			default:
				assert(false && "Unknown Root signature declaration option");
				break;
			}

			return std::make_tuple(Parsing::Option::NumDecl, 1);
		};

		parser["RSDescNumDecl"] = [](const peg::SemanticValues& sv)
		{
			return peg::any_cast<int>(sv[0]);
		};

		// --- Resources
		parser["VertAttr"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			
			parseCtx.passSources.back().vertAttr.emplace_back(Parsing::VertAttr{
				peg::any_cast<std::string>(sv[0]),
				std::move(peg::any_cast<std::vector<Parsing::VertAttrField>>(sv[1])),
				sv.str()
				});
		};

		parser["Resource"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			std::tuple<Parsing::ResourceScope, Parsing::ResourceBindFrequency> resourceAttr =
				peg::any_cast<std::tuple<Parsing::ResourceScope, Parsing::ResourceBindFrequency>>(sv[0]);

			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			if (sv[1].type() == typeid(Parsing::Resource_ConstBuff))
			{
				currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_ConstBuff>(sv[1]));
			}
			else if (sv[1].type() == typeid(Parsing::Resource_Texture))
			{
				currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_Texture>(sv[1]));
			}
			else if (sv[1].type() == typeid(Parsing::Resource_Sampler))
			{
				currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_Sampler>(sv[1]));
			}
			else
			{
				assert(false && "Resource callback invalid type. Local scope");
			}

			SetResourceBindFrequency(currentPass.resources.back(), std::get<Parsing::ResourceBindFrequency>(resourceAttr));
			SetResourceScope(currentPass.resources.back(), std::get<Parsing::ResourceScope>(resourceAttr));

		};

		parser["ConstBuff"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_ConstBuff{
				peg::any_cast<std::string>(sv[0]),
				std::nullopt,
				std::nullopt,
				peg::any_cast<int>(sv[1]),
				peg::any_cast<std::vector<RootArg::ConstBuffField>>(sv[2]),
				sv.str()};
		};

		parser["Texture"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_Texture{
				peg::any_cast<std::string>(sv[0]),
				std::nullopt,
				std::nullopt,
				peg::any_cast<int>(sv[1]),
				sv.str()};
		};


		parser["Sampler"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_Sampler{
				peg::any_cast<std::string>(sv[0]),
				std::nullopt,
				std::nullopt,
				peg::any_cast<int>(sv[1]),
				sv.str()};
		};

		parser["ResourceAttr"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(
				peg::any_cast<Parsing::ResourceScope>(sv[0]),
				peg::any_cast<Parsing::ResourceBindFrequency>(sv[1]));
		};

		parser["ResourceScope"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<Parsing::ResourceScope>(sv.choice());
		};

		parser["ResourceUpdate"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<Parsing::ResourceBindFrequency>(sv.choice());
		};

		parser["ConstBuffContent"] = [](const peg::SemanticValues& sv)
		{
			std::vector<RootArg::ConstBuffField> constBufferContent;

			std::transform(sv.begin(), sv.end(), std::back_inserter(constBufferContent),
				[](const peg::any& token)
			{
				const std::tuple<Parsing::DataType, std::string>& dataField = peg::any_cast<std::tuple<Parsing::DataType, std::string>>(token);

				return RootArg::ConstBuffField{
					Parsing::GetParseDataTypeSize(std::get<Parsing::DataType>(dataField)),
					HASH(std::get<std::string>(dataField).c_str())
				};
			});

			return constBufferContent;
		};

		parser["ConstBuffField"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(peg::any_cast<Parsing::DataType>(sv[0]), peg::any_cast<std::string>(sv[1]));
		};

		parser["VertAttrContent"] = [](const peg::SemanticValues& sv)
		{
			std::vector<Parsing::VertAttrField> content;

			std::transform(sv.begin(), sv.end(), std::back_inserter(content),
				[](const peg::any& field) { return std::any_cast<Parsing::VertAttrField>(field); });

			return content;
		};

		parser["VertAttrField"] = [](const peg::SemanticValues& sv) 
		{
			std::string name = peg::any_cast<std::string>(sv[1]);
			std::tuple<std::string, unsigned int> semanticInfo = 
				peg::any_cast<std::tuple<std::string, unsigned int>>(sv[2]);

			return Parsing::VertAttrField{
				peg::any_cast<Parsing::DataType>(sv[0]),
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
			return static_cast<Parsing::DataType>(sv.choice());
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
			return peg::any_cast<int>(sv[0]);
		};

		parser["RegisterId"] = [](const peg::SemanticValues& sv) 
		{
			return peg::any_cast<int>(sv[0]);
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

	void InitFrameGraphSourceParser(peg::parser& parser)
	{
		// Load grammar
		const std::string frameGraphGrammar = ReadFile(FrameGraphBuilder::Inst().GenPathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_FRAMEGRAPH_FILENAME));

		parser.log = [](size_t line, size_t col, const std::string& msg)
		{
			Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

			assert(false && "Parsing error");
		};

		const bool loadGrammarResult = parser.load_grammar(frameGraphGrammar.c_str());
		assert(loadGrammarResult && "Can't load pass grammar");

		parser["FrameGraph"] = [](const peg::SemanticValues& sv, peg::any& ctx)
		{
			Parsing::FrameGraphSourceContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::FrameGraphSourceContext>&>(ctx);
			
			std::for_each(sv.begin(), sv.end(),
				[&parseCtx](const peg::any& pass) 
			{
				parseCtx.passes.push_back(peg::any_cast<std::string>(pass));
			});
		};

		parser["Pass"] = [](const peg::SemanticValues& sv)
		{
			return sv.token();
		};

	};

	
}

void FrameGraphBuilder::AddRootArg(PassParameters& pass, FrameGraph& frameGraph,
	Parsing::ResourceBindFrequency updateFrequency, Parsing::ResourceScope scope, RootArg::Arg_t&& arg)
{
	switch (scope)
	{
	case Parsing::ResourceScope::Local:
	{
		switch (updateFrequency)
		{
		case Parsing::ResourceBindFrequency::PerObject:
			pass.perObjectLocalRootArgsTemplate.push_back(std::move(arg));
			break;
		case Parsing::ResourceBindFrequency::PerPass:
			pass.passRootArgs.push_back(std::move(arg));
			break;
		default:
			assert(false && "Undefined bind frequency handling in add root arg pass. Local");
			break;
		}
	}
	break;
	case Parsing::ResourceScope::Global:
	{
		switch (updateFrequency)
		{
		case Parsing::ResourceBindFrequency::PerObject:
		{
			// This is global so try to find if resource for this was already created
			std::vector<RootArg::Arg_t>& perObjGlobalResTemplate =
				frameGraph.objGlobalResTemplate[static_cast<int>(*pass.input)];

			int resTemplateIndex = RootArg::FindArg(perObjGlobalResTemplate, arg);
			if (resTemplateIndex == Const::INVALID_INDEX)
			{
				// Res is not found create new
				perObjGlobalResTemplate.push_back(std::move(arg));

				// Add proper index
				pass.perObjGlobalRootArgsIndicesTemplate.push_back(perObjGlobalResTemplate.size() - 1);
			}
			else
			{
				pass.perObjGlobalRootArgsIndicesTemplate.push_back(resTemplateIndex);
			}

		}
		break;
		case Parsing::ResourceBindFrequency::PerPass:
		{
			int resIndex = RootArg::FindArg(frameGraph.passesGlobalRes, arg);

			if (resIndex == Const::INVALID_INDEX)
			{
				// Res is not found create new
				frameGraph.passesGlobalRes.push_back(std::move(arg));

				// Add proper index
				pass.passGlobalRootArgsIndices.push_back(frameGraph.passesGlobalRes.size() - 1);
			}
			else
			{
				pass.passGlobalRootArgsIndices.push_back(resIndex);
			}
		}
		break;
		default:
			assert(false && "Undefined bind frequency handling in add root arg pass. Global");
			break;
		}
	}
	break;
	default:
		assert(false && "Can't add root arg, no scope");
		break;
	}
}


void FrameGraphBuilder::ValidateResources(const std::vector<PassParametersSource>& passesParametersSources) const
{
#ifdef _DEBUG
	
	std::vector<Parsing::Resource_t> globalResources;

	// Check for name collision
	for (const PassParametersSource& paramSource : passesParametersSources)
	{
		for (const Parsing::Resource_t& currentRes : paramSource.resources)
		{
			std::string_view currentResName = Parsing::GetResourceName(currentRes);
			
			// In pass collision check
			{
				const int count = std::count_if(paramSource.resources.cbegin(), 
					paramSource.resources.cend(), [currentResName](const Parsing::Resource_t& res) 
				{
					return currentResName == Parsing::GetResourceName(res);
				});

				assert(count == 1 && "Name collision inside pass resource declaration");
			}

			// Global collision check
			{
				// Check if we have resource with the same name
				const auto resIt = std::find_if(globalResources.cbegin(), globalResources.cend(),
					[currentResName](const Parsing::Resource_t& res)
				{
					return currentResName == Parsing::GetResourceName(res);
				});

				if (resIt != globalResources.cend())
				{
					// Make sure content is equal. If yes, then this is just the same resource,
					// if no then we have name collision
					assert(Parsing::IsEqual(*resIt, currentRes) && "Global resource name collision is found");
				}
				else
				{
					// No such resource were found. Add this one to the list
					globalResources.push_back(currentRes);
				}
			}


		}
	}

#endif
}

FrameGraphBuilder::PassCompiledShaders_t FrameGraphBuilder::CompileShaders(const PassParametersSource& pass) const
{
	PassCompiledShaders_t passCompiledShaders;

	for (const PassParametersSource::ShaderSource& shader : pass.shaders)
	{
		std::string resToInclude;

		// Add External Resources
		for (const std::string& externalRes : shader.externals)
		{
			// Find resource and stub it into shader source

			// Try actual resources
			const auto resIt = std::find_if(pass.resources.cbegin(), pass.resources.cend(),
				[&externalRes](const Parsing::Resource_t& currRes)
			{
				return externalRes == Parsing::GetResourceName(currRes);
			});

			if (resIt != pass.resources.cend()) 
			{
				resToInclude += Parsing::GetResourceRawView(*resIt);
			}
			else
			{
				// Not found among resources, try vert attr
				const auto vertAttrIt = std::find_if(pass.vertAttr.cbegin(), pass.vertAttr.cend(), 
					[&externalRes](const Parsing::VertAttr& vertAttr)
				{
					return vertAttr.name == externalRes;

				});

				assert(vertAttrIt != pass.vertAttr.cend() && "Shader compilation failed can't find resource");

				resToInclude += vertAttrIt->rawView;

			}

			resToInclude += ";";
		}

		std::string sourceCode =
			resToInclude +
			"[RootSignature( \" " + pass.rootSignature->rawView + " \" )]" +
			shader.source;

		// Got final shader source, now compile
		ComPtr<ID3DBlob>& shaderBlob = passCompiledShaders.emplace_back(std::make_pair(shader.type, ComPtr<ID3DBlob>())).second;
		ComPtr<ID3DBlob> errors;

		const std::string strShaderType = PassParametersSource::ShaderTypeToStr(shader.type);

		HRESULT hr = D3DCompile(
			sourceCode.c_str(),
			sourceCode.size(),
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

	return passCompiledShaders;
}

FrameGraphBuilder::FrameGraphBuilder()
{
	std::string pathToThisFile = __FILE__;
	ROOT_DIR_PATH = pathToThisFile.substr(0, pathToThisFile.rfind("\\"));
}

FrameGraphBuilder& FrameGraphBuilder::Inst()
{
	static FrameGraphBuilder* matCompiler = nullptr;

	if (matCompiler == nullptr)
	{
		matCompiler = new FrameGraphBuilder();
	}

	return *matCompiler;
}

FrameGraph FrameGraphBuilder::BuildFrameGraph()
{
	return CompileFrameGraph(GenerateFrameGraphSource());
}

FrameGraph FrameGraphBuilder::CompileFrameGraph(FrameGraphSource&& source) const
{
	FrameGraph frameGraph;

	ValidateResources(source.passesParametersSources);

	// Add passes to frame graph in proper order
	for (const std::string& passName : source.passes)
	{
		auto passParamIt = std::find_if(source.passesParametersSources.begin(), source.passesParametersSources.end(), 
			[&passName](const PassParametersSource& paramSource)
		{
			return paramSource.name == passName;
		});

		assert(passParamIt != source.passesParametersSources.end() && "Can't find PassParameters source for pass creation");

		PassParameters passParam = CompilePassParameters(std::move(*passParamIt), frameGraph);

		// Add pass
		switch (*passParam.input)
		{
		case Parsing::PassInputType::UI:
		{
			frameGraph.passes.emplace_back(Pass_UI{});
		}
		break;
		default:
			assert(false && "Pass with undefined input is detected");
			break;
		}

		// Init pass
		std::visit([&passParam](auto&& pass)
		{
			pass.Init(std::move(passParam));

		}, frameGraph.passes.back());

	}

	return frameGraph;
}

FrameGraphSource FrameGraphBuilder::GenerateFrameGraphSource() const
{
	FrameGraphSource frameGraphSource;

	frameGraphSource.passesParametersSources = GeneratePassesParameterSources();

	std::shared_ptr<Parsing::FrameGraphSourceContext> parseCtx = ParseFrameGraphFile(LoadFrameGraphFile());

	frameGraphSource.passes = parseCtx->passes;

	return frameGraphSource;
}

std::vector<PassParametersSource> FrameGraphBuilder::GeneratePassesParameterSources() const
{
	std::shared_ptr<Parsing::PassParametersContext> parseCtx = ParsePassFiles(LoadPassFiles());

	std::vector<PassParametersSource> passesParametersSources;

	for (PassParametersSource& passParameterSource : parseCtx->passSources)
	{
		passesParametersSources.push_back(std::move(passParameterSource));
	}

	return passesParametersSources;
}

std::unordered_map<std::string, std::string> FrameGraphBuilder::LoadPassFiles() const
{
	std::unordered_map<std::string, std::string> passFiles;

	for (const auto& file : std::filesystem::directory_iterator(GenPathToFile(Settings::FRAMEGRAPH_DIR)))
	{
		const std::filesystem::path filePath = file.path();

		if (filePath.extension() == Settings::FRAMEGRAPH_PASS_FILE_EXT)
		{
			std::string passFileContent = ReadFile(filePath);

			passFiles.emplace(std::make_pair(
				filePath.filename().string(),
				std::move(passFileContent)));
		}
	}

	return passFiles;
}

std::string FrameGraphBuilder::LoadFrameGraphFile() const
{
	for (const auto& file : std::filesystem::directory_iterator(GenPathToFile(Settings::FRAMEGRAPH_DIR)))
	{
		const std::filesystem::path filePath = file.path();

		if (filePath.extension() == Settings::FRAMEGRAPH_FILE_EXT)
		{
			std::string frameGraphFileContent = ReadFile(filePath);

			return frameGraphFileContent;
		}
	}

	assert(false && "Material file was not found");

	return std::string();
}

std::shared_ptr<Parsing::PassParametersContext> FrameGraphBuilder::ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles) const
{
	peg::parser parser;
	InitPassParser(parser);

	std::shared_ptr<Parsing::PassParametersContext> context = std::make_shared<Parsing::PassParametersContext>();

	for (const auto& passFile : passFiles)
	{
		//#DEBUG only UI
		std::string passName = passFile.first.substr(0, passFile.first.rfind('.'));

		if (passName != "UI")
		{
			continue;
		}
		//END

		context->passSources.emplace_back(PassParametersSource()).name = passFile.first.substr(0, passFile.first.rfind('.'));

		peg::any ctx = context;

		parser.parse(passFile.second.c_str(), ctx);
	}

	return context;
}

std::shared_ptr<Parsing::FrameGraphSourceContext> FrameGraphBuilder::ParseFrameGraphFile(const std::string& frameGraphSourceFileContent) const
{
	peg::parser parser;
	InitFrameGraphSourceParser(parser);

	std::shared_ptr<Parsing::FrameGraphSourceContext> context = std::make_shared<Parsing::FrameGraphSourceContext>();
	peg::any ctx = context;

	parser.parse(frameGraphSourceFileContent.c_str(), ctx);

	return context;
}

std::vector<D3D12_INPUT_ELEMENT_DESC> FrameGraphBuilder::GenerateInputLayout(const PassParametersSource& pass) const
{
	const Parsing::VertAttr& vertAttr = GetPassInputVertAttr(pass);

	assert((pass.vertAttrSlots.empty() || pass.vertAttrSlots.size() == vertAttr.content.size())
		&& "Invalid vert attr slots num, for input layout generation");

	std::array<unsigned int, 16> inputSlotOffset;
	std::fill(inputSlotOffset.begin(), inputSlotOffset.end(), 0);

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	
	for (int i = 0; i < vertAttr.content.size(); ++i)
	{
		const Parsing::VertAttrField& field = vertAttr.content[i];

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
				Parsing::GetParseDataTypeDXGIFormat(field.type),
				static_cast<unsigned>(inputSlot),
				inputSlotOffset[inputSlot],
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
				0U
			});

		inputSlotOffset[inputSlot] += Parsing::GetParseDataTypeSize(field.type);
	}

	return inputLayout;
}

const Parsing::VertAttr& FrameGraphBuilder::GetPassInputVertAttr(const PassParametersSource& pass) const
{
	const std::string& inputName = pass.inputVertAttr;

	const auto attrIt = std::find_if(pass.vertAttr.cbegin(), pass.vertAttr.cend(),
		[inputName](const Parsing::VertAttr& attr) { return inputName == attr.name; });

	assert(attrIt != pass.vertAttr.cend() && "Can't find input vert attribute");

	return *attrIt;
}

ComPtr<ID3D12RootSignature> FrameGraphBuilder::GenerateRootSignature(const PassParametersSource& pass, const PassCompiledShaders_t& shaders) const
{
	assert(shaders.empty() == false && "Can't generate root signature with not shaders");

	ComPtr<ID3D12RootSignature> rootSig;

	const ComPtr<ID3DBlob>& shaderBlob = shaders.front().second;

	Infr::Inst().GetDevice()->CreateRootSignature(0, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSig.GetAddressOf()));

	return rootSig;
}

ComPtr<ID3D12PipelineState> FrameGraphBuilder::GeneratePipelineStateObject(const PassParametersSource& passSource, PassCompiledShaders_t& shaders, ComPtr<ID3D12RootSignature>& rootSig) const
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = passSource.psoDesc;

	// Set up root sig
	psoDesc.pRootSignature = rootSig.Get();
	
	// Set up shaders
	for (const std::pair<PassParametersSource::ShaderType, ComPtr<ID3DBlob>>& shaders : shaders)
	{
		D3D12_SHADER_BYTECODE shaderByteCode = {
			reinterpret_cast<BYTE*>(shaders.second->GetBufferPointer()),
			shaders.second->GetBufferSize()
		};

		switch (shaders.first)
		{
		case PassParametersSource::Vs:
			psoDesc.VS = shaderByteCode;
			break;
		case PassParametersSource::Gs:
			psoDesc.GS = shaderByteCode;
			break;
		case PassParametersSource::Ps:
			psoDesc.PS = shaderByteCode;
			break;
		default:
			assert(false && "Generate pipeline state object. Invalid shader type");
			break;
		}
	}

	// Set up input layout
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = GenerateInputLayout(passSource);
	psoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };

	ComPtr<ID3D12PipelineState> pipelineState;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

	return pipelineState;
}

void FrameGraphBuilder::CreateResourceArguments(const PassParametersSource& passSource, FrameGraph& frameGraph, PassParameters& pass) const
{
	const std::vector<Parsing::Resource_t>& passResources = passSource.resources;

	for (int i = 0; i < passSource.rootSignature->params.size(); ++i)
	{
		const Parsing::RootParma_t& rootParam = passSource.rootSignature->params[i];

		std::visit([paramIndex = i, &passResources, &pass, &frameGraph](auto&& rootParam)
		{
			using T = std::decay_t<decltype(rootParam)>;

			if constexpr (std::is_same_v<T, Parsing::RootParam_ConstBuffView>)
			{
				const Parsing::Resource_ConstBuff* res =
					FindResourceOfTypeAndRegId<Parsing::Resource_ConstBuff>(passResources, rootParam.registerId);

				assert(rootParam.num == 1 && "Const buffer view should always have numDescriptors 1");

				AddRootArg(pass,
					frameGraph,
					*res->bindFrequency,
					*res->scope ,
					RootArg::ConstBuffView{
						paramIndex,
						HASH(res->name.c_str()),
						res->content,
						Const::INVALID_BUFFER_HANDLER
				});

			}
			else if constexpr (std::is_same_v<T, Parsing::RootParam_DescTable>)
			{
				RootArg::DescTable descTableArgument;
				descTableArgument.bindIndex = paramIndex;
				
				std::optional<Parsing::ResourceBindFrequency> bindFrequency;
				std::optional<Parsing::ResourceScope> scope;
				//#TODO RootArg here are created when for all global objects they might already exist
				// so this is a bit redundant. Should check if res already exist first. 
				// Maybe it is not taking too much resources and not worth to worry?
				for (const Parsing::DescTableEntity_t& descTableEntity : rootParam.entities) 
				{
					std::visit([&descTableArgument, &bindFrequency, &scope, &passResources, &pass](auto&& descTableParam) 
					{
						using T = std::decay_t<decltype(descTableParam)>;

						if constexpr (std::is_same_v<T, Parsing::RootParam_ConstBuffView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_ConstBuff* res =
									FindResourceOfTypeAndRegId<Parsing::Resource_ConstBuff>(passResources, descTableParam.registerId + i);

								// Set or validate update frequency
								if (bindFrequency.has_value() == false)
								{
									bindFrequency = res->bindFrequency;
								}
								else
								{
									assert(*bindFrequency == res->bindFrequency && "All resources in desc table should have the same bind frequency");
								}

								if (scope.has_value() == false)
								{
									scope = res->scope;
								}
								else
								{
									assert(*scope == res->scope && "All resources in desc table should have the same scope");
								}
								
								descTableArgument.content.emplace_back(RootArg::DescTableEntity_ConstBufferView{
									HASH(res->name.c_str()),
									res->content,
									Const::INVALID_BUFFER_HANDLER,
									Const::INVALID_INDEX
									});
							}
						}
						else if constexpr (std::is_same_v<T, Parsing::RootParam_TextView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_Texture* res =
									FindResourceOfTypeAndRegId<Parsing::Resource_Texture>(passResources, descTableParam.registerId + i);

								// Set or validate update frequency
								if (bindFrequency.has_value() == false)
								{
									bindFrequency = res->bindFrequency;
								}
								else
								{
									assert(*bindFrequency == res->bindFrequency && "All resources in desc table should have the same update frequency");
								}

								if (scope.has_value() == false)
								{
									scope = res->scope;
								}
								else
								{
									assert(*scope == res->scope && "All resources in desc table should have the same scope");
								}

								descTableArgument.content.emplace_back(RootArg::DescTableEntity_Texture{
									HASH(res->name.c_str())						
								});
							}

						}
						else if constexpr (std::is_same_v<T, Parsing::RootParam_SamplerView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_Sampler* res =
									FindResourceOfTypeAndRegId<Parsing::Resource_Sampler>(passResources, descTableParam.registerId + i);

								// Set or validate update frequency
								if (bindFrequency.has_value() == false)
								{
									bindFrequency = res->bindFrequency;
								}
								else
								{
									assert(*bindFrequency == res->bindFrequency && "All resources in desc table should have the same update frequency");
								}

								if (scope.has_value() == false)
								{
									scope = res->scope;
								}
								else
								{
									assert(*scope == res->scope && "All resources in desc table should have the same scope");
								}

								descTableArgument.content.emplace_back(RootArg::DescTableEntity_Sampler{
									HASH(res->name.c_str())
									});
							}
						}
						else
						{
							static_assert(false, "Invalid desc table entity type");
						}

					}, descTableEntity);
				}

				AddRootArg(pass, frameGraph, *bindFrequency, *scope, descTableArgument);
			}
			else
			{
				static_assert(false, "Resource argument can't be created. Invalid root param type");
			}


		}, rootParam);
	}
}

PassParameters FrameGraphBuilder::CompilePassParameters(PassParametersSource&& passSource, FrameGraph& frameGraph) const
{
	PassParameters passParam;

	passParam.input = passSource.input;
	passParam.name = passSource.name;
	passParam.primitiveTopology = passSource.primitiveTopology;
	passParam.colorTargetNameHash = HASH(passSource.colorTargetName.c_str());
	passParam.depthTargetNameHash = HASH(passSource.depthTargetName.c_str());
	passParam.viewport = passSource.viewport;
	passParam.vertAttr = GetPassInputVertAttr(passSource);

	PassCompiledShaders_t compiledShaders = CompileShaders(passSource);
	passParam.rootSingature = GenerateRootSignature(passSource, compiledShaders);
	passParam.pipelineState = GeneratePipelineStateObject(passSource, compiledShaders, passParam.rootSingature);


	CreateResourceArguments(passSource, frameGraph, passParam);

	return passParam;
}

std::filesystem::path FrameGraphBuilder::GenPathToFile(const std::string fileName) const
{
	std::filesystem::path path = ROOT_DIR_PATH;
	path.append(fileName);

	return path;
}
