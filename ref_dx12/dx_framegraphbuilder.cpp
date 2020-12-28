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
	void ValidateResource(const T& resource, PassParametersSource::ResourceScope scope, const Parsing::PassParametersContext& parseCtx)
	{
#ifdef _DEBUG

		std::string_view resourceName = resource.name;

		switch (scope)
		{
		case PassParametersSource::ResourceScope::Local:
		{
			// Local resource can't collide with local resources in the same pass
			// or with any global resource
			const PassParametersSource& currentPassSource = parseCtx.passSources.back();

			// Check against same pass resources
			assert(std::find_if(currentPassSource.resources.cbegin(), currentPassSource.resources.cend(),
				[resourceName](const Parsing::Resource_t& existingRes) 
			{
				return resourceName == PassParametersSource::GetResourceName(existingRes);

			}) == currentPassSource.resources.cend() &&
				"Local to local resource name collision");

			// Check against other globals
			assert(std::find_if(parseCtx.resources.cbegin(), parseCtx.resources.cend(),
				[resourceName](const Parsing::Resource_t& existingRes)
			{
				return  resourceName == PassParametersSource::GetResourceName(existingRes);

			}) == parseCtx.resources.cend() &&
				"Global to local resource name collision, local insertion");

			break;
		}
		case PassParametersSource::ResourceScope::Global:
		{
			// Global resource can't collide with other global resource and with any local resource

			for(const PassParametersSource& currentPassSource : parseCtx.passSources)
			{
				// Check against same pass resources
				assert(std::find_if(currentPassSource.resources.cbegin(), currentPassSource.resources.cend(),
					[resourceName](const Parsing::Resource_t& existingRes)
				{
					return resourceName == PassParametersSource::GetResourceName(existingRes);

				}) == currentPassSource.resources.cend() &&
					"Global to local resource name collision, global insertion");
			}

			// Check against other globals
			assert(std::find_if(parseCtx.resources.cbegin(), parseCtx.resources.cend(),
				[resourceName](const Parsing::Resource_t& existingRes)
			{
				return resourceName == PassParametersSource::GetResourceName(existingRes);

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

	template<typename T1>
	const T1* FindResourceForRootArgument(const std::vector<Parsing::Resource_t>& passResources, const std::vector<Parsing::Resource_t>& globalRes, int registerId)
	{
		const T1* res = FindResourceOfTypeAndRegId<T1>(passResources, registerId);
		if (res == nullptr)
		{
			res = FindResourceOfTypeAndRegId<T1>(globalRes, registerId);
		}

		assert(res != nullptr && "Can't find resource for root argument");

		return res;
	}

	template<typename T>
	void AddRootArgToPass(PassParameters& pass, Parsing::ResourceUpdate updateFrequency, T&& res)
	{
		switch (updateFrequency)
		{
		case Parsing::ResourceUpdate::PerObject:
			pass.perObjectRootArgsTemplate.push_back(std::move(res));
			break;
		case Parsing::ResourceUpdate::PerPass:
			pass.passRootArgs.push_back(std::move(res));
			break;
		case Parsing::ResourceUpdate::OnInit:
		default:
			assert(false && "Unimplemented update frequency handling in add root arg pass");
			break;
		}
	}

	void SetResourceUpdateFrequency(Parsing::Resource_t& r, Parsing::ResourceUpdate update)
	{
		std::visit([update](auto&& resource) 
		{
			resource.updateFrequency = update;
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
			parseCtx.passSources.back().input = static_cast<PassParametersSource::InputType>(sv.choice());
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
			//#DEBUG ignore ResourceUpdate for now. Handle Scope only. Do debug validation
			std::tuple<PassParametersSource::ResourceScope, Parsing::ResourceUpdate> resourceAttr =
				peg::any_cast<std::tuple<PassParametersSource::ResourceScope, Parsing::ResourceUpdate>>(sv[0]);

			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			switch (std::get<PassParametersSource::ResourceScope>(resourceAttr))
			{
			case PassParametersSource::ResourceScope::Local:
			{
				if (sv[1].type() == typeid(Parsing::Resource_ConstBuff))
				{
					ValidateResource(std::any_cast<Parsing::Resource_ConstBuff>(sv[1]), PassParametersSource::ResourceScope::Local, parseCtx);
					currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_ConstBuff>(sv[1]));
				}
				else if (sv[1].type() == typeid(Parsing::Resource_Texture))
				{
					ValidateResource(std::any_cast<Parsing::Resource_Texture>(sv[1]), PassParametersSource::ResourceScope::Local, parseCtx);
					currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_Texture>(sv[1]));
				}
				else if (sv[1].type() == typeid(Parsing::Resource_Sampler))
				{
					ValidateResource(std::any_cast<Parsing::Resource_Sampler>(sv[1]), PassParametersSource::ResourceScope::Local, parseCtx);
					currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_Sampler>(sv[1]));
				}
				else
				{
					assert(false && "Resource callback invalid type. Local scope");
				}

				SetResourceUpdateFrequency(currentPass.resources.back(), std::get<Parsing::ResourceUpdate>(resourceAttr));

				break;
			}
			case PassParametersSource::ResourceScope::Global:
			{
				if (sv[1].type() == typeid(Parsing::Resource_ConstBuff))
				{
					ValidateResource(std::any_cast<Parsing::Resource_ConstBuff>(sv[1]), PassParametersSource::ResourceScope::Global, parseCtx);
					parseCtx.resources.emplace_back(std::any_cast<Parsing::Resource_ConstBuff>(sv[1]));
				}
				else if (sv[1].type() == typeid(Parsing::Resource_Texture))
				{
					ValidateResource(std::any_cast<Parsing::Resource_Texture>(sv[1]), PassParametersSource::ResourceScope::Global, parseCtx);
					parseCtx.resources.emplace_back(std::any_cast<Parsing::Resource_Texture>(sv[1]));
				}
				else if (sv[1].type() == typeid(Parsing::Resource_Sampler))
				{
					ValidateResource(std::any_cast<Parsing::Resource_Sampler>(sv[1]), PassParametersSource::ResourceScope::Global, parseCtx);
					parseCtx.resources.emplace_back(std::any_cast<Parsing::Resource_Sampler>(sv[1]));
				}
				else
				{
					assert(false && "Resource callback invalid type. Global scope");
				}

				SetResourceUpdateFrequency(parseCtx.resources.back(), std::get<Parsing::ResourceUpdate>(resourceAttr));

				break;
			}
			default:
				assert(false && "Resource callback undefined resource scope type");
				break;
			}

		};

		parser["ConstBuff"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_ConstBuff{
				peg::any_cast<std::string>(sv[0]),
				Parsing::ResourceUpdate::OnInit,
				peg::any_cast<int>(sv[1]),
				peg::any_cast<std::vector<RootArg::ConstBuffField>>(sv[2]),
				sv.str()};
		};

		parser["Texture"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_Texture{
				peg::any_cast<std::string>(sv[0]),
				Parsing::ResourceUpdate::OnInit,
				peg::any_cast<int>(sv[1]),
				sv.str()};
		};


		parser["Sampler"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_Sampler{
				peg::any_cast<std::string>(sv[0]),
				Parsing::ResourceUpdate::OnInit,
				peg::any_cast<int>(sv[1]),
				sv.str()};
		};

		parser["ResourceAttr"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(
				peg::any_cast<PassParametersSource::ResourceScope>(sv[0]),
				peg::any_cast<Parsing::ResourceUpdate>(sv[1]));
		};

		parser["ResourceScope"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<PassParametersSource::ResourceScope>(sv.choice());
		};

		parser["ResourceUpdate"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<Parsing::ResourceUpdate>(sv.choice());
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


FrameGraphBuilder::PassCompiledShaders_t FrameGraphBuilder::CompileShaders(const PassParametersSource& pass, const std::vector<Parsing::Resource_t>& globalRes) const
{
	PassCompiledShaders_t passCompiledShaders;

	for (const PassParametersSource::ShaderSource& shader : pass.shaders)
	{
		std::string resToInclude;

		// Add External Resources
		for (const std::string& externalRes : shader.externals)
		{
			// Find resource and stub it into shader source

			// Search in local scope first
			const auto localResIt = std::find_if(pass.resources.cbegin(), pass.resources.cend(),
				[externalRes](const Parsing::Resource_t& currRes)
			{
				return externalRes == PassParametersSource::GetResourceName(currRes);
			});

			if (localResIt != pass.resources.cend())
			{
				resToInclude += PassParametersSource::GetResourceRawView(*localResIt);
				resToInclude += ";";
			}
			else
			{
				// Search in global scope
				const auto globalResIt = std::find_if(globalRes.cbegin(), globalRes.cend(),
					[externalRes](const Parsing::Resource_t& currRes)
				{
					return externalRes == PassParametersSource::GetResourceName(currRes);
				});

				if (globalResIt != globalRes.cend())
				{
					resToInclude += PassParametersSource::GetResourceRawView(*globalResIt);
					resToInclude += ";";
				}
				else
				{
					// Finally try vert attributes
					const auto vertAttrIt = std::find_if(pass.vertAttr.cbegin(), pass.vertAttr.cend(),
						[externalRes](const Parsing::VertAttr& currVert)
					{
						return externalRes == currVert.name;
					});

					assert(vertAttrIt != pass.vertAttr.cend() && "External resource can't be found");

					resToInclude += vertAttrIt->rawView;
					resToInclude += ";";
				}
			}
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

	frameGraph.passes.clear();

	for (int i = 0; i < source.passesParameters.size(); ++i)
	{
		PassParameters& passParameters = source.passesParameters[i];

		switch (passParameters.input)
		{
		case PassParametersSource::InputType::UI:
		{
			Pass_t& pass = frameGraph.passes.emplace_back(Pass_UI{});
			InitPass(std::move(passParameters), pass);
		}
		break;
		case PassParametersSource::InputType::Undefined:
		{
			assert(false && "Pass with undefined input is detected");
		}
		break;
		default:
			break;
		}
	}

	return frameGraph;
}

FrameGraphSource FrameGraphBuilder::GenerateFrameGraphSource() const
{
	FrameGraphSource frameGraphSource;

	std::shared_ptr<Parsing::FrameGraphSourceContext> parseCtx = ParseFrameGraphFile(LoadFrameGraphFile());
	std::vector<PassParameters> passes = GeneratePassesParameters();

	for (const std::string& passName : parseCtx->passes)
	{
		auto targetPassIt = std::find_if(passes.begin(), passes.end(),
			[passName](const PassParameters& p)
		{
			return passName == p.name;
		});

		assert(targetPassIt != passes.cend() && "Can't generate material, target pass is not found");

		frameGraphSource.passesParameters.push_back(std::move(*targetPassIt));
	}

	return frameGraphSource;
}

std::vector<PassParameters> FrameGraphBuilder::GeneratePassesParameters() const
{
	std::shared_ptr<Parsing::PassParametersContext> parseCtx = ParsePassFiles(LoadPassFiles());

	std::vector<PassParameters> passesParameters;

	for (const PassParametersSource& passSource : parseCtx->passSources)
	{
		passesParameters.push_back(CompilePass(passSource, parseCtx->resources));
	}

	return passesParameters;
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

void FrameGraphBuilder::PreprocessPassFiles(const std::vector<std::string>& fileList)
{
	//#DEBUG going to make it work without preprocess first. And the add preprocessing
}

//#DEBUG not sure returning context is fine
std::shared_ptr<Parsing::PassParametersContext> FrameGraphBuilder::ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles) const
{
	peg::parser parser;
	InitPassParser(parser);

	std::shared_ptr<Parsing::PassParametersContext> context = std::make_shared<Parsing::PassParametersContext>();

	for (const auto& passFile : passFiles)
	{
		//#DEBUG
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

void FrameGraphBuilder::CreateResourceArguments(const PassParametersSource& passSource, const std::vector<Parsing::Resource_t>& globalRes, PassParameters& pass) const
{
	const std::vector<Parsing::Resource_t>& passResources = passSource.resources;

	for (int i = 0; i < passSource.rootSignature->params.size(); ++i)
	{
		const Parsing::RootParma_t& rootParam = passSource.rootSignature->params[i];

		std::visit([paramIndex = i, &passResources, &pass, &globalRes](auto&& rootParam)
		{
			using T = std::decay_t<decltype(rootParam)>;

			if constexpr (std::is_same_v<T, Parsing::RootParam_ConstBuffView>)
			{
				const Parsing::Resource_ConstBuff* res =
					FindResourceForRootArgument<Parsing::Resource_ConstBuff>(passResources, globalRes, rootParam.registerId);

				assert(rootParam.num == 1 && "Const buffer view should always have numDescriptors 1");

				AddRootArgToPass(pass, res->updateFrequency, RootArg::ConstBuffView{
					paramIndex,
					HASH(res->name.c_str()),
					res->content,
					Const::INVALID_BUFFER_HANDLER
				});

			}
			else if constexpr (std::is_same_v<T, Parsing::RootParam_DescTable>)
			{
				RootArg::DescTable descTableArgument;
				descTableArgument.index = paramIndex;
				//#DEBUG so far it will be updated on every entity. The idea is that everything in
				// the same desc table should have the same update frequency. I need to find some way to validate this
				// and enforce this stuff, and set it up
				Parsing::ResourceUpdate updateFrequency = Parsing::ResourceUpdate::OnInit;


				for (const Parsing::DescTableEntity_t& descTableEntity : rootParam.entities) 
				{
					std::visit([&descTableArgument, &updateFrequency, &passResources, &pass, &globalRes](auto&& descTableParam) 
					{
						using T = std::decay_t<decltype(descTableParam)>;

						if constexpr (std::is_same_v<T, Parsing::RootParam_ConstBuffView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_ConstBuff* res =
									FindResourceForRootArgument<Parsing::Resource_ConstBuff>(passResources, globalRes, descTableParam.registerId + i);

								updateFrequency = res->updateFrequency;

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
									FindResourceForRootArgument<Parsing::Resource_Texture>(passResources, globalRes, descTableParam.registerId + i);

								updateFrequency = res->updateFrequency;

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
									FindResourceForRootArgument<Parsing::Resource_Sampler>(passResources, globalRes, descTableParam.registerId + i);

								updateFrequency = res->updateFrequency;

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

				AddRootArgToPass(pass, updateFrequency, descTableArgument);
			}
			else
			{
				static_assert(false, "Resource argument can't be created. Invalid root param type");
			}


		}, rootParam);
	}
}

PassParameters FrameGraphBuilder::CompilePass(const PassParametersSource& passSource, const std::vector<Parsing::Resource_t>& globalRes) const
{
	PassParameters pass;

	pass.input = passSource.input;
	pass.name = passSource.name;
	pass.primitiveTopology = passSource.primitiveTopology;
	pass.colorTargetNameHash = HASH(passSource.colorTargetName.c_str());
	pass.depthTargetNameHash = HASH(passSource.depthTargetName.c_str());
	pass.viewport = passSource.viewport;
	pass.vertAttr = GetPassInputVertAttr(passSource);

	PassCompiledShaders_t compiledShaders = CompileShaders(passSource, globalRes);
	pass.rootSingature = GenerateRootSignature(passSource, compiledShaders);
	pass.pipelineState = GeneratePipelineStateObject(passSource, compiledShaders, pass.rootSingature);
	CreateResourceArguments(passSource, globalRes, pass);

	return pass;
}

void FrameGraphBuilder::InitPass(PassParameters&& passParameters, Pass_t& pass) const
{
	std::visit([&passParameters](auto&& pass)
	{
		pass.passParameters = std::move(passParameters);

		pass.Init();

	}, pass);
}

std::filesystem::path FrameGraphBuilder::GenPathToFile(const std::string fileName) const
{
	std::filesystem::path path = ROOT_DIR_PATH;
	path.append(fileName);

	return path;
}
