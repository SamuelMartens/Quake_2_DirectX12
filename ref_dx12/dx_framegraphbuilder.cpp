#include "dx_framegraphbuilder.h"

#include <fstream>


#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include "Lib/crc32.h"
#include "Lib/peglib.h"
#include "dx_app.h"

#include <limits>

namespace
{

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

	template<typename T>
	void SetScopeAndBindFrequency(std::optional<Parsing::ResourceBindFrequency>& bindFrequency, std::optional<Parsing::ResourceScope>& scope, const T& res)
	{
		// Set or validate update frequency
		if (bindFrequency.has_value() == false)
		{
			bindFrequency = res->bindFrequency;
		}
		else
		{
			DX_ASSERT(*bindFrequency == res->bindFrequency && "All resources in desc table should have the same bind frequency");
		}

		if (scope.has_value() == false)
		{
			scope = res->scope;
		}
		else
		{
			DX_ASSERT(*scope == res->scope && "All resources in desc table should have the same scope");
		}
	}

	void SetResourceBind(Parsing::Resource_t& r, const std::string& bind)
	{
		std::visit([&bind](auto&& resource)
		{
			resource.bind = bind;
		}, r);
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

	void AlignConstBufferFields(Parsing::Resource_ConstBuff& constBuffer)
	{
		// According to HLSL documentation, max alignment go to 16 bytes
		constexpr int MAX_FIELD_ALIGNMENT = 16;

		DX_ASSERT(constBuffer.content.empty() == false && "Trying to align empty constant buffer");

		std::vector<int> offsets;
		offsets.reserve(constBuffer.content.size());

		int maxAlignment = 0;
		int alignedOffset = 0;

		// Get aligned offsets for each field
		for (const RootArg::ConstBuffField& f : constBuffer.content)
		{
			const int fieldAlignment = std::min<int>(f.size, MAX_FIELD_ALIGNMENT);
			maxAlignment = std::max(maxAlignment, fieldAlignment);

			alignedOffset = Utils::Align(alignedOffset, fieldAlignment);
			offsets.push_back(alignedOffset);

			alignedOffset += f.size;
		}

		for (int i = 0; i < constBuffer.content.size() - 1; ++i)
		{
			constBuffer.content[i].size = offsets[i + 1] - offsets[i];
		}
		// Fix up last size so the struct overall size is correct
		// Padding will be added so the struct overall is aligned to the biggest alignment
		constBuffer.content.back().size = Utils::Align(alignedOffset, maxAlignment) - offsets.back();
	}

	template<typename T, int ComponentsNum>
	std::vector<std::byte> CreateInitBuffer(const int elementsNum, const T* initValue)
	{
		std::vector<std::byte> initBuffer(elementsNum * sizeof(T) * ComponentsNum);

		for (int i = 0; i < elementsNum; ++i)
		{
			for (int j = 0; j < ComponentsNum; ++j)
			{
				reinterpret_cast<T*>(initBuffer.data())[i * ComponentsNum + j] = initValue[j];
			}
		}

		return initBuffer;
	}

	void InitPreprocessorParser(peg::parser& parser)
	{
		// Load grammar
		const std::string preprocessorGrammar = Utils::ReadFile(Utils::GenAbsolutePathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_PREPROCESSOR_FILENAME));

		parser.set_logger([](size_t line, size_t col, const std::string& msg)
		{
			Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

			DX_ASSERT(false && "Preprocessing error");
		});

		const bool loadGrammarResult = parser.load_grammar(preprocessorGrammar.c_str());
		DX_ASSERT(loadGrammarResult && "Can't load pass grammar");

		// Set up callbacks
		parser["Instruction"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PreprocessorContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PreprocessorContext>&>(ctx);

			DX_ASSERT(parseCtx.currentFile.empty() == false && "Current file for preprocessor parser is empty");

			// So far I have only include instructions
			auto instruction = std::any_cast<Parsing::PreprocessorContext::Include>(sv[1]);

			// Account for start definition symbol, so correct position and length
			instruction.len += 1;
			instruction.pos -= 1;

			parseCtx.includes[parseCtx.currentFile].push_back(std::move(instruction));
		};


		parser["IncludeInstr"] = [](const peg::SemanticValues& sv)
		{
			std::string includeFilename = std::any_cast<std::string>(sv[0]) + "." + std::any_cast<std::string>(sv[1]);

			return Parsing::PreprocessorContext::Include
			{
				std::move(includeFilename),
				std::distance(sv.ss, sv.sv().data()),
				// NOTE: include length also contains trailing new line symbols,
				// so preprocessed file might be not exactly what you expect (some symbols might be lost)
				static_cast<int>(sv.sv().size())
			};

		};

		parser["Word"] = [](const peg::SemanticValues& sv)
		{
			return sv.token_to_string();
		};
	}

	void InitPassParser(peg::parser& parser) 
	{
		// Load grammar
		const std::string passGrammar = Utils::ReadFile(Utils::GenAbsolutePathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_PASS_FILENAME));

		parser.set_logger([](size_t line, size_t col, const std::string& msg)
		{
			Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

			ThrowIfFalse(false);
		});

		const bool loadGrammarResult = parser.load_grammar(passGrammar.c_str());
		DX_ASSERT(loadGrammarResult && "Can't load pass grammar");

		// Set up callbacks

		// --- Top level pass tokens
		parser["PassInputIdent"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().input = static_cast<Parsing::PassInputType>(sv.choice());
		};

		parser["PassVertAttr"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().inputVertAttr = std::any_cast<std::string>(sv[0]);
		};

		parser["PassVertAttrSlots"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().vertAttrSlots = std::move(std::any_cast<std::vector<std::tuple<unsigned int, int>>>(sv[0]));
		};

		parser["PassThreadGroups"] = [](const peg::SemanticValues& sv, std::any& ctx) 
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass =  parseCtx.passSources.back();
			
			std::array<int,3> threadGroups;

			for (int i = 0; i < threadGroups.size(); ++i) 
			{
				threadGroups[i] = std::any_cast<int>(sv[i]);
			}

			currentPass.threadGroups = threadGroups;
		};

		// --- State
		parser["ColorTargetSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);

			std::vector<std::string>& colorTargetNames = parseCtx.passSources.back().colorTargetNames;
			std::vector<DXGI_FORMAT>& colorTargetFormats = parseCtx.passSources.back().colorTargetFormats;

			colorTargetNames.reserve(sv.size());
			colorTargetFormats.reserve(sv.size());

			// Fill up names
			for (int i = 0; i < sv.size(); ++i) 
			{
				colorTargetNames.push_back(std::any_cast<std::string>(sv[i]));
			}

			// Fill up formats
			for (const std::string& targetName : colorTargetNames)
			{
				if (targetName == PassParameters::COLOR_BACK_BUFFER_NAME)
				{
					// Is back buffer?
					colorTargetFormats.push_back(Settings::BACK_BUFFER_FORMAT);
				}
				else if (Resource* resource = ResourceManager::Inst().FindResource(targetName))
				{
					// Is one of existing resource?
					colorTargetFormats.push_back(resource->desc.format);
				}
				else
				{
					// Is internal resource?
					DX_ASSERT(parseCtx.frameGraphContext != nullptr);

					const std::vector<FrameGraphSource::FrameGraphResourceDecl>& internalResourceDecl = parseCtx.frameGraphContext->resources;

					const auto resourceDeclIt = std::find_if(internalResourceDecl.cbegin(), internalResourceDecl.cend(), 
						[&targetName](const FrameGraphSource::FrameGraphResourceDecl& decl) 
					{
						return decl.name == targetName;
					});

					// This is last branch, so if we fail here we can't find resource at all.
					DX_ASSERT(resourceDeclIt != internalResourceDecl.cend() && "Can't find required render target resource");
					
					colorTargetFormats.push_back(resourceDeclIt->desc.Format);
				}
			}
		};

		parser["DepthTargetSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			parseCtx.passSources.back().depthTargetName = std::any_cast<std::string>(sv[0]);
		};

		parser["ViewportSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			// This might be a bit buggy. I am pretty sure that camera viewport is always equal to drawing area
			// but I might not be the case.
			int width, height;
			Renderer::Inst().GetDrawAreaSize(&width, &height);

			currentPass.viewport.TopLeftX = sv[0].type() == typeid(int) ?
				std::any_cast<int>(sv[0]) : std::any_cast<float>(sv[0]) * width;

			currentPass.viewport.TopLeftY = sv[1].type() == typeid(int) ?
				std::any_cast<int>(sv[1]) : std::any_cast<float>(sv[1]) * height;

			currentPass.viewport.Width = sv[2].type() == typeid(int) ?
				std::any_cast<int>(sv[2]) : std::any_cast<float>(sv[2]) * width;

			currentPass.viewport.Height = sv[3].type() == typeid(int) ?
				std::any_cast<int>(sv[3]) : std::any_cast<float>(sv[3]) * height;

			DX_ASSERT(currentPass.viewport.TopLeftX < currentPass.viewport.Width  && "Weird viewport X param, are you sure?");
			DX_ASSERT(currentPass.viewport.TopLeftY < currentPass.viewport.Height  && "Weird viewport Y param, are you sure?");
		};

		parser["BlendEnabledSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.BlendState.RenderTarget[0].BlendEnable = std::any_cast<bool>(sv[0]);
		};

		parser["SrcBlendSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.BlendState.RenderTarget[0].SrcBlend = std::any_cast<D3D12_BLEND>(sv[0]);
		};

		parser["DestBlendSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.BlendState.RenderTarget[0].DestBlend = std::any_cast<D3D12_BLEND>(sv[0]);
		};

		parser["TopologySt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			auto topology = std::any_cast<std::tuple<D3D_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE>>(sv[0]);

			currentPass.primitiveTopology = std::get<D3D_PRIMITIVE_TOPOLOGY>(topology);
			currentPass.rasterPsoDesc.PrimitiveTopologyType = std::get<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(topology);
		};

		parser["DepthWriteMaskSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.DepthStencilState.DepthWriteMask = std::any_cast<bool>(sv[0]) ?
				D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		};

		parser["DepthBiasSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.RasterizerState.DepthBias = std::any_cast<int>(sv[0]);
		};

		parser["DepthBiasSlopeSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.RasterizerState.SlopeScaledDepthBias = std::any_cast<float>(sv[0]);
		};

		parser["DepthBiasClampSt"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			currentPass.rasterPsoDesc.RasterizerState.DepthBiasClamp = std::any_cast<float>(sv[0]);
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
				DX_ASSERT(false && "Invalid blend state");
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
			case 2:
				return std::make_tuple(D3D_PRIMITIVE_TOPOLOGY_LINELIST, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
			default:
				DX_ASSERT(false && "Invalid topology state");
				break;
			}

			return std::make_tuple(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		};

		// --- Shader code

		parser["ShaderExternalDecl"] = [](const peg::SemanticValues& sv)
		{
			std::vector<std::string> externalList;

			for (int i = 0; i < sv.size(); ++i)
			{
				externalList.push_back(std::any_cast<std::string>(sv[i]));
			}

			return externalList;
		};

		parser["ShaderSource"] = [](const peg::SemanticValues& sv)
		{
			return sv.token_to_string();
		};

		parser["Shader"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			PassParametersSource::ShaderSource& shaderSource = currentPass.shaders.emplace_back(PassParametersSource::ShaderSource());

			shaderSource.type = std::any_cast<PassParametersSource::ShaderType>(sv[0]);
			shaderSource.externals = std::move(std::any_cast<std::vector<std::string>>(sv[1]));
			shaderSource.source = std::move(std::any_cast<std::string>(sv[2]));
		};

		parser["ShaderType"] = [](const peg::SemanticValues& sv)
		{
			DX_ASSERT(sv.choice() < PassParametersSource::ShaderType::SIZE && "Error during parsing shader type");

			return static_cast<PassParametersSource::ShaderType>(sv.choice());
		};

		parser["ShaderTypeDecl"] = [](const peg::SemanticValues& sv)
		{
			return std::any_cast<PassParametersSource::ShaderType>(sv[0]);
		};

		// --- Root Signature
		parser["RSig"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			Parsing::RootSignature& rootSig = *parseCtx.passSources.back().rootSignature;

			rootSig.rawView = sv.token_to_string();
			// Later root signature is inserted into shader source code. It must be in one line
			// otherwise shader wouldn't compile
			rootSig.rawView.erase(std::remove(rootSig.rawView.begin(), rootSig.rawView.end(), '\n'), rootSig.rawView.end());

			std::for_each(sv.begin() + 1, sv.end(),
				[&rootSig](const std::any& token) 
			{
				if (token.type() == typeid(Parsing::RootParam_ConstBuffView))
				{
					Parsing::RootParam_ConstBuffView cbv = std::any_cast<Parsing::RootParam_ConstBuffView>(token);
					DX_ASSERT(cbv.num == 1 && "CBV Inline descriptor can't have more that 1 num");

					rootSig.params.push_back(std::move(cbv));
				}
				else if (token.type() == typeid(Parsing::RootParam_DescTable))
				{
					Parsing::RootParam_DescTable descTable = std::any_cast<Parsing::RootParam_DescTable>(token);
					rootSig.params.push_back(std::move(descTable));
				}
				else if (token.type() == typeid(Parsing::RootParam_ShaderResourceView))
				{
					Parsing::RootParam_ShaderResourceView srv = std::any_cast<Parsing::RootParam_ShaderResourceView>(token);
					DX_ASSERT(srv.num == 1 && "SRV Inline descriptor can't have more that 1 num");

					rootSig.params.push_back(std::move(srv));
				}
				else if (token.type() == typeid(Parsing::RootParam_UAView))
				{
					DX_ASSERT(false && "UAV inline descriptor is not supported currently");
				}
				else 
				{
					DX_ASSERT(false && "Invalid root parameter");
				};
			});
		};


		parser["RSigStatSamplerDecl"] = [](const peg::SemanticValues& sv)
		{
			DX_ASSERT(false && "Static samplers are not implemented");
		};

		parser["RSigRootConstDecl"] = [](const peg::SemanticValues& sv) 
		{
			DX_ASSERT(false && "Root constants are not implemented");
		};

		parser["RSigDescTableDecl"] = [](const peg::SemanticValues& sv)
		{
			Parsing::RootParam_DescTable descTable;

			std::for_each(sv.begin(), sv.end(),
				[&descTable](const std::any& token)
			{
				if (token.type() == typeid(Parsing::RootParam_ShaderResourceView))
				{
					descTable.entities.push_back(std::any_cast<Parsing::RootParam_ShaderResourceView>(token));
				}
				else if (token.type() == typeid(Parsing::RootParam_ConstBuffView))
				{
					descTable.entities.push_back(std::any_cast<Parsing::RootParam_ConstBuffView>(token));
				}
				else if (token.type() == typeid(Parsing::RootParam_SamplerView))
				{
					descTable.entities.push_back(std::any_cast<Parsing::RootParam_SamplerView>(token));
				}
				else if (token.type() == typeid(Parsing::RootParam_UAView))
				{
					descTable.entities.push_back(std::any_cast<Parsing::RootParam_UAView>(token));
				}
				else if (token.type() == typeid(std::tuple<Parsing::Option, int>))
				{

				}
				else
				{
					DX_ASSERT(false && "Unknown type for desc table entity");
				}
			});

			return descTable;
		};

		parser["RSigCBVDecl"] = [](const peg::SemanticValues& sv)
		{
			int num = 1;

			for (int i = 1 ; i < sv.size(); ++i)
			{
				auto option = std::any_cast<std::tuple<Parsing::Option, int>>(sv[i]);

				switch (std::get<Parsing::Option>(option))
				{
				case Parsing::Option::NumDecl:
					num = std::get<int>(option);
					break;
				case Parsing::Option::Visibility:
					break;
				default:
					DX_ASSERT(false && "Invalid root param option in CBV decl");
					break;
				}
			}

			return Parsing::RootParam_ConstBuffView{
				std::any_cast<int>(sv[0]),
				num
			};
		};

		parser["RSigSRVDecl"] = [](const peg::SemanticValues& sv)
		{
			int num = 1;

			for (int i = 1; i < sv.size(); ++i)
			{
				auto option = std::any_cast<std::tuple<Parsing::Option, int>>(sv[i]);

				switch (std::get<Parsing::Option>(option))
				{
				case Parsing::Option::NumDecl:
					num = std::get<int>(option);
					break;
				case Parsing::Option::Visibility:
					break;
				default:
					DX_ASSERT(false && "Invalid root param option in SRV decl");
					break;
				}
			}

			return Parsing::RootParam_ShaderResourceView{
				std::any_cast<int>(sv[0]),
				num
			};
		};

		parser["RSigUAVDecl"] = [](const peg::SemanticValues& sv) 
		{
			int num = 1;

			for (int i = 1; i < sv.size(); ++i)
			{
				auto option = std::any_cast<std::tuple<Parsing::Option, int>>(sv[i]);

				switch (std::get<Parsing::Option>(option))
				{
				case Parsing::Option::NumDecl:
					num = std::get<int>(option);
					break;
				case Parsing::Option::Visibility:
					break;
				default:
					DX_ASSERT(false && "Invalid root param option in UAV decl");
					break;
				}
			}

			return Parsing::RootParam_UAView{
				std::any_cast<int>(sv[0]),
				num
			};
		};

		parser["RSigDescTableSampler"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::RootParam_SamplerView{
				std::any_cast<int>(sv[0]),
				sv.size() == 1 ? 1 :  std::any_cast<int>(sv[1])
			};
		};

		parser["RSigDeclOptions"] = [](const peg::SemanticValues& sv)
		{
			switch (sv.choice())
			{
			case 0:
				return std::make_tuple(Parsing::Option::Visibility, 0);
			case 1:
				return std::make_tuple(Parsing::Option::NumDecl, std::any_cast<int>(sv[0]));
			default:
				DX_ASSERT(false && "Unknown Root signature declaration option");
				break;
			}

			return std::make_tuple(Parsing::Option::NumDecl, 1);
		};

		parser["RSDescNumDecl"] = [](const peg::SemanticValues& sv)
		{
			return std::any_cast<int>(sv[0]);
		};

		// --- PrePostPass
		parser["PrePass"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			std::vector<PassParametersSource::FixedFunction_t>& prePass = parseCtx.passSources.back().prePassFuncs;

			for (int i = 0; i < sv.size(); ++i)
			{
				prePass.push_back(std::any_cast<PassParametersSource::FixedFunction_t>(sv[i]));
			}
		};

		parser["PostPass"] = [](const peg::SemanticValues& sv, std::any& ctx) 
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			std::vector<PassParametersSource::FixedFunction_t>& postPass = parseCtx.passSources.back().postPassFuncs;

			for (int i = 0; i < sv.size(); ++i)
			{
				postPass.push_back(std::any_cast<PassParametersSource::FixedFunction_t>(sv[i]));
			}
		};

		parser["FixedFunction"] = [](const peg::SemanticValues& sv)
		{
			return std::any_cast<PassParametersSource::FixedFunction_t>(sv[0]);
		};

		parser["FixedFunctionClearColor"] = [](const peg::SemanticValues& sv)
		{
			return PassParametersSource::FixedFunction_t{
				PassParametersSource::FixedFunctionClearColor{
					std::any_cast<XMFLOAT4>(sv[0]),
			} };
		};

		parser["FixedFunctionClearDepth"] = [](const peg::SemanticValues& sv)
		{
			return PassParametersSource::FixedFunction_t{
				PassParametersSource::FixedFunctionClearDepth{
					std::any_cast<float>(sv[0]),
			} };
		};


		// --- ShaderDefs
		parser["Function"] = [](const peg::SemanticValues& sv, std::any& ctx) 
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);

			parseCtx.passSources.back().functions.emplace_back(Parsing::Function{
				std::any_cast<std::string>(sv[1]),
				std::string(sv.sv())
				});
		};

		parser["VertAttr"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			
			parseCtx.passSources.back().vertAttr.emplace_back(Parsing::VertAttr{
				std::any_cast<std::string>(sv[0]),
				std::move(std::any_cast<std::vector<Parsing::VertAttrField>>(sv[1])),
				std::string(sv.sv())
				});
		};

		parser["Resource"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			auto resourceAttr =
				std::any_cast<std::tuple<
				Parsing::ResourceScope,
				Parsing::ResourceBindFrequency,
				std::optional<std::string>>>(sv[0]);

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
			else if (sv[1].type() == typeid(Parsing::Resource_RWTexture))
			{
				currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_RWTexture>(sv[1]));
			}
			else if (sv[1].type() == typeid(Parsing::Resource_StructuredBuffer))
			{
				currentPass.resources.emplace_back(std::any_cast<Parsing::Resource_StructuredBuffer>(sv[1]));
			}
			else
			{
				DX_ASSERT(false && "Resource callback invalid type. Local scope");
			}

			Parsing::Resource_t& currentRes = currentPass.resources.back();

			SetResourceBindFrequency(currentRes, std::get<Parsing::ResourceBindFrequency>(resourceAttr));
			SetResourceScope(currentRes, std::get<Parsing::ResourceScope>(resourceAttr));

			const auto& resBind = std::get<std::optional<std::string>>(resourceAttr);

			if (resBind.has_value())
			{
				SetResourceBind(currentRes, resBind.value());
			}

		};

		parser["StructuredBuff"] = [](const peg::SemanticValues& sv) 
		{
			return Parsing::Resource_StructuredBuffer{
				std::any_cast<std::string>(sv[1]),
				std::nullopt,
				std::nullopt,
				std::nullopt,
				std::any_cast<int>(sv[2]),
				std::string(sv.sv()),
				std::any_cast<Parsing::StructBufferDataType_t>(sv[0])
			};
		};

		parser["ConstBuff"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_ConstBuff{
				std::any_cast<std::string>(sv[0]),
				std::nullopt,
				std::nullopt,
				std::nullopt,
				std::any_cast<int>(sv[1]),
				std::string(sv.sv()),
				std::any_cast<std::vector<RootArg::ConstBuffField>>(sv[2])};
		};

		parser["Texture"] = [](const peg::SemanticValues& sv)
		{
			const int tokenSize = sv.size();

			return Parsing::Resource_Texture{
				std::any_cast<std::string>(sv[tokenSize - 2]),
				std::nullopt,
				std::nullopt,
				std::nullopt,
				std::any_cast<int>(sv[tokenSize - 1]),
				std::string(sv.sv())};
		};

		parser["RWTexture"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_RWTexture{
				std::any_cast<std::string>(sv[1]),
				std::nullopt,
				std::nullopt,
				std::nullopt,
				std::any_cast<int>(sv[2]),
				std::string(sv.sv()) };
		};

		parser["Sampler"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::Resource_Sampler{
				std::any_cast<std::string>(sv[0]),
				std::nullopt,
				std::nullopt,
				std::nullopt,
				std::any_cast<int>(sv[1]),
				std::string(sv.sv())};
		};

		parser["ResourceAttr"] = [](const peg::SemanticValues& sv)
		{
			auto attr = std::make_tuple(
				std::any_cast<Parsing::ResourceScope>(sv[0]),
				std::any_cast<Parsing::ResourceBindFrequency>(sv[1]),
				std::optional<std::string>(std::nullopt));

			if (sv.size() > 2)
			{
				std::get<2>(attr) = std::any_cast<std::string>(sv[2]);
			}

			return attr;
		};

		parser["ResourceScope"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<Parsing::ResourceScope>(sv.choice());
		};

		parser["ResourceUpdate"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<Parsing::ResourceBindFrequency>(sv.choice());
		};

		parser["StructBufferType"] = [](const peg::SemanticValues& sv)
		{
			if (sv[0].type() == typeid(Parsing::DataType))
			{
				return Parsing::StructBufferDataType_t(std::any_cast<Parsing::DataType>(sv[0]));
			}
			else if (sv[0].type() == typeid(std::string))
			{
				return Parsing::StructBufferDataType_t(std::any_cast<std::string>(sv[0]));
			}
			else
			{
				DX_ASSERT(false && "Invalid StructBufferType");
				return Parsing::StructBufferDataType_t("");
			}
		};

		parser["ConstBuffContent"] = [](const peg::SemanticValues& sv)
		{
			std::vector<RootArg::ConstBuffField> constBufferContent;

			std::transform(sv.begin(), sv.end(), std::back_inserter(constBufferContent),
				[](const std::any& token)
			{
				const std::tuple<Parsing::DataType, std::string>& dataField = std::any_cast<std::tuple<Parsing::DataType, std::string>>(token);

				return RootArg::ConstBuffField{
					Parsing::GetParseDataTypeSize(std::get<Parsing::DataType>(dataField)),
					HASH(std::get<std::string>(dataField).c_str())
				};
			});

			return constBufferContent;
		};

		parser["ResourceBind"] = [](const peg::SemanticValues& sv)
		{
			return sv[0];
		};

		parser["ConstBuffField"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(std::any_cast<Parsing::DataType>(sv[0]), std::any_cast<std::string>(sv[1]));
		};

		parser["VertAttrContent"] = [](const peg::SemanticValues& sv)
		{
			std::vector<Parsing::VertAttrField> content;

			std::transform(sv.begin(), sv.end(), std::back_inserter(content),
				[](const std::any& field) { return std::any_cast<Parsing::VertAttrField>(field); });

			return content;
		};

		parser["VertAttrField"] = [](const peg::SemanticValues& sv) 
		{
			std::string name = std::any_cast<std::string>(sv[1]);
			std::tuple<std::string, unsigned int> semanticInfo = 
				std::any_cast<std::tuple<std::string, unsigned int>>(sv[2]);

			return Parsing::VertAttrField{
				std::any_cast<Parsing::DataType>(sv[0]),
				HASH(name.c_str()),
				std::get<std::string>(semanticInfo),
				std::get<unsigned int>(semanticInfo),
				std::move(name)
			};
		};

		parser["VertAttrSlots"] = [](const peg::SemanticValues& sv) 
		{
			std::vector<std::tuple<unsigned int, int>> result;

			for (int i = 0; i < sv.size(); ++i)
			{
				result.push_back(std::any_cast<std::tuple<unsigned int, int>>(sv[i]));
			}

			return result;
		};

		parser["VertAttrFieldSlot"] = [](const peg::SemanticValues& sv) 
		{
			return std::make_tuple(HASH(std::any_cast<std::string>(sv[0]).c_str()), std::any_cast<int>(sv[1]));
		};

		// --- Misc Defs

		parser["MiscDef"] = [](const peg::SemanticValues& sv, std::any& ctx) 
		{
			Parsing::PassParametersContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::PassParametersContext>&>(ctx);
			PassParametersSource& currentPass = parseCtx.passSources.back();

			if (sv[0].type() == typeid(Parsing::MiscDef_Struct))
			{
				currentPass.miscDefs.emplace_back(std::any_cast<Parsing::MiscDef_Struct>(sv[0]));
			}
			else
			{
				DX_ASSERT(false && "MiscDef invalid type");
			}
		};

		parser["Struct"] = [](const peg::SemanticValues& sv)
		{
			return Parsing::MiscDef_Struct{
				std::any_cast<std::string>(sv[0]),
				std::any_cast<std::vector<Parsing::StructField>>(sv[1]),
				std::string(sv.sv())
			};
		};

		parser["StructContent"] = [](const peg::SemanticValues& sv)
		{
			std::vector<Parsing::StructField> content;

			std::transform(sv.begin(), sv.end(), std::back_inserter(content),
				[](const std::any& field) { return std::any_cast<Parsing::StructField>(field); });

			return content;
		};

		parser["StructField"] = [](const peg::SemanticValues& sv) 
		{
			std::string name = std::any_cast<std::string>(sv[1]);

			return Parsing::StructField{
				std::any_cast<Parsing::DataType>(sv[0]),
				HASH(name.c_str()),
				std::move(name)
			};
		};

		parser["DataType"] = [](const peg::SemanticValues& sv)
		{
			return static_cast<Parsing::DataType>(sv.choice());
		};

		parser["ResourceFieldSemantic"] = [](const peg::SemanticValues& sv)
		{
			return std::make_tuple(std::any_cast<std::string>(sv[0]), 
				sv.size() > 1 ? static_cast<unsigned int>(std::any_cast<int>(sv[1])) : 0);
		};

		// --- Tokens
		parser["Ident"] = [](const peg::SemanticValues& sv)
		{
			return sv.token_to_string();
		};

		parser["RegisterDecl"] = [](const peg::SemanticValues& sv)
		{
			return std::any_cast<int>(sv[0]);
		};

		parser["RegisterId"] = [](const peg::SemanticValues& sv) 
		{
			return std::any_cast<int>(sv[0]);
		};

		// -- Types
		parser["Bool"] = [](const peg::SemanticValues& sv)
		{
			return sv.choice() == 0;
		};

		parser["Float"] = [](const peg::SemanticValues& sv)
		{
			return stof(sv.token_to_string());
		};

		parser["Int"] = [](const peg::SemanticValues& sv)
		{
			return stoi(sv.token_to_string());
		};

		parser["Word"] = [](const peg::SemanticValues& sv)
		{
			return sv.token_to_string();
		};

		parser["Float4"] = [](const peg::SemanticValues& sv)
		{
			return XMFLOAT4
			{
				std::any_cast<float>(sv[0]),
				std::any_cast<float>(sv[1]),
				std::any_cast<float>(sv[2]),
				std::any_cast<float>(sv[3]),
			};
		};
	};

	void InitFrameGraphParser(peg::parser& parser)
	{
		// Load grammar
		const std::string grammar = Utils::ReadFile(Utils::GenAbsolutePathToFile(Settings::GRAMMAR_DIR + "/" + Settings::GRAMMAR_FRAMEGRAPH_FILENAME));

		parser.set_logger([](size_t line, size_t col, const std::string& msg)
		{
			Logs::Logf(Logs::Category::Parser, "Error: line %d , col %d %s", line, col, msg.c_str());

			DX_ASSERT(false && "FrameGraph parsing error");
		});

		const bool loadGrammarResult = parser.load_grammar(grammar.c_str());
		DX_ASSERT(loadGrammarResult && "Can't load FrameGraph grammar");

		parser["RenderStep"] = [](const peg::SemanticValues& sv, std::any& ctx)
		{
			Parsing::FrameGraphSourceContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::FrameGraphSourceContext>&>(ctx);
			
			std::for_each(sv.begin(), sv.end(),
				[&parseCtx](const std::any& pass) 
			{
				parseCtx.steps.push_back(std::any_cast<FrameGraphSource::Step_t>(pass));
			});
		};

		parser["Pass"] = [](const peg::SemanticValues& sv)
		{
			return FrameGraphSource::Step_t{ 
				FrameGraphSource::Pass{ std::any_cast<std::string>(sv[0]) } };
		};

		parser["FixedFunctionCopy"] = [](const peg::SemanticValues& sv)
		{
			return FrameGraphSource::Step_t{
				FrameGraphSource::FixedFunctionCopy{
					std::any_cast<std::string>(sv[0]),
					std::any_cast<std::string>(sv[1])
			} };
		};

		// --- Resource Declaration
		parser["ResourceDecls"] = [](const peg::SemanticValues& sv, std::any& ctx) 
		{
			Parsing::FrameGraphSourceContext& parseCtx = *std::any_cast<std::shared_ptr<Parsing::FrameGraphSourceContext>&>(ctx);

			std::for_each(sv.begin(), sv.end(),
				[&parseCtx](const std::any& resource)
			{
				parseCtx.resources.push_back(std::move(std::any_cast<FrameGraphSource::FrameGraphResourceDecl>(resource)));
			});
		};

		parser["ResourceDecl"] = [](const peg::SemanticValues& sv)
		{
			auto dimensions = std::any_cast<std::array<int,3>>(sv[2]);

			D3D12_RESOURCE_DESC desc = {};
			desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Alignment = 0;
			
			desc.Width = dimensions[0];
			desc.Height = dimensions[1];
			desc.DepthOrArraySize = dimensions[2];
			
			desc.Dimension = std::any_cast<D3D12_RESOURCE_DIMENSION>(sv[1]);
			desc.Format = std::any_cast<DXGI_FORMAT>(sv[3]);
			desc.Flags = std::any_cast<D3D12_RESOURCE_FLAGS>(sv[4]);
			
			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) 
			{
				desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			}

			std::optional<XMFLOAT4> clearValue = std::any_cast<std::optional<XMFLOAT4>>(sv[5]);
			std::optional<XMFLOAT4> initValue = std::any_cast<std::optional<XMFLOAT4>>(sv[6]);


			return FrameGraphSource::FrameGraphResourceDecl{ 
				std::any_cast<std::string>(sv[0]),
				desc,
				clearValue,
				initValue
			};
		};

		parser["ResourceDeclType"] = [](const peg::SemanticValues& sv) 
		{
			switch (HASH(std::any_cast<std::string>(sv[0]).c_str())) 
			{
			case HASH("Texture2D"):
				return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			case HASH("Buffer"):
				return D3D12_RESOURCE_DIMENSION_BUFFER;
			default:
				DX_ASSERT(false && "Unknown value of ResourceDeclType");
			}

			return D3D12_RESOURCE_DIMENSION_BUFFER;
		};

		parser["ResourceDeclDimen"] = [](const peg::SemanticValues& sv) 
		{
			std::array<int, 3> dimensions;

			dimensions[0] = std::any_cast<int>(sv[0]);
			dimensions[1] = sv.size() > 1 ? std::any_cast<int>(sv[1]) : 1;
			dimensions[2] = sv.size() > 2 ? std::any_cast<int>(sv[2]) : 1;

			return dimensions;
		};

		parser["ResourceDeclFormat"] = [](const peg::SemanticValues& sv) 
		{
			switch (HASH(std::any_cast<std::string>(sv[0]).c_str()))
			{
			case HASH("UNKNOWN"):
				return DXGI_FORMAT_UNKNOWN;
			case HASH("R8G8B8A8_UNORM"):
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case HASH("R32G32_FLOAT"):
				return DXGI_FORMAT_R32G32_FLOAT;
			case HASH("R32G32B32_FLOAT"):
				return DXGI_FORMAT_R32G32B32_FLOAT;
			case HASH("R32G32B32A32_FLOAT"):
				return DXGI_FORMAT_R32G32B32A32_FLOAT;
			case HASH("R16_UINT"):
				return DXGI_FORMAT_R16_UINT;
			case HASH("R16_FLOAT"):
				return DXGI_FORMAT_R16_FLOAT;
			case HASH("R16_SINT"):
				return DXGI_FORMAT_R16_SINT;
			case HASH("D24_UNORM_S8_UINT"):
				return DXGI_FORMAT_D24_UNORM_S8_UINT;
			default:
				DX_ASSERT(false && "Invalid value of ResourceDeclFormat");
				break;
			}

			return DXGI_FORMAT_UNKNOWN;
		};

		parser["ResourceDeclFlags"] = [](const peg::SemanticValues& sv)
		{
			D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

			for (int i = 0; i < sv.size(); ++i)
			{
				switch (HASH(std::any_cast<std::string>(sv[i]).c_str()))
				{
				case HASH("NONE"):
					flags |= D3D12_RESOURCE_FLAG_NONE;
					break;
				case HASH("ALLOW_RENDER_TARGET"):
					flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
					break;
				case HASH("ALLOW_DEPTH_STENCIL"):
					flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
					break;
				case HASH("ALLOW_UNORDERED_ACCESS"):
					flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
					break;
				case HASH("DENY_SHADER_RESOURCE"):
					flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
					break;
				default:
					DX_ASSERT(false && "Unknown flag in resource declaration");
					break;
				}
			}

			return flags;
		};

		parser["ResourceDeclClearVal"] = [](const peg::SemanticValues& sv)
		{
			std::optional<XMFLOAT4> clearValue;

			if (sv.empty() == true)
			{
				return clearValue;
			}


			if (sv[0].type() == typeid(XMFLOAT4))
			{
				clearValue = std::any_cast<XMFLOAT4>(sv[0]);
			}

			return clearValue;
		};

		parser["ResourceDeclInitVal"] = [](const peg::SemanticValues& sv)
		{
			std::optional<XMFLOAT4> initValue;

			if (sv.empty() == true)
			{
				return initValue;
			}

			if (sv[0].type() == typeid(XMFLOAT4))
			{
				initValue = std::any_cast<XMFLOAT4>(sv[0]);
			}

			return initValue;
		};

		// --- Tokens
		parser["Ident"] = [](const peg::SemanticValues& sv)
		{
			return sv.token_to_string();
		};

		// --- Types
		parser["Int"] = [](const peg::SemanticValues& sv)
		{
			return stoi(sv.token_to_string());
		};

		parser["Float"] = [](const peg::SemanticValues& sv)
		{
			return stof(sv.token_to_string());
		};

		parser["Float4"] = [](const peg::SemanticValues& sv)
		{
			return XMFLOAT4
			{
				std::any_cast<float>(sv[0]),
				std::any_cast<float>(sv[1]),
				std::any_cast<float>(sv[2]),
				std::any_cast<float>(sv[3]),
			};
		};
	};

	template<Parsing::PassInputType INPUT_TYPE>
	void _AddRootArg(PassParameters& pass, std::vector<RootArg::Arg_t>& passesGlobalRes, FrameGraph::PerObjectGlobalTemplate_t& objGlobalResTemplate,
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
				pass.passLocalRootArgs.push_back(std::move(arg));
				break;
			default:
				DX_ASSERT(false && "Undefined bind frequency handling in add root arg pass. Local");
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
				auto& perObjGlobalResTemplate =
					std::get<static_cast<int>(INPUT_TYPE)>(objGlobalResTemplate);

				PassParameters::AddGlobalPerObjectRootArgIndex(
					pass.perObjGlobalRootArgsIndicesTemplate,
					perObjGlobalResTemplate, std::move(arg));

			}
			break;
			case Parsing::ResourceBindFrequency::PerPass:
			{
				const int resIndex = RootArg::FindArg(passesGlobalRes, arg);

				if (resIndex == Const::INVALID_INDEX)
				{
					// Res is not found create new
					passesGlobalRes.push_back(std::move(arg));

					// Add proper index
					pass.passGlobalRootArgsIndices.push_back(passesGlobalRes.size() - 1);
				}
				else
				{
					DX_ASSERT(RootArg::GetBindIndex(arg) == RootArg::GetBindIndex(passesGlobalRes[resIndex]) &&
					"Global pass resources must have same bind indexes. Seems like different passes, place them differently.");

					pass.passGlobalRootArgsIndices.push_back(resIndex);
				}
			}
			break;
			default:
				DX_ASSERT(false && "Undefined bind frequency handling in add root arg pass. Global");
				break;
			}
		}
		break;
		default:
			DX_ASSERT(false && "Can't add root arg, no scope");
			break;
		}
	}
}

void FrameGraphBuilder::AddRootArg(PassParameters& pass, FrameGraph& frameGraph,
	Parsing::ResourceBindFrequency updateFrequency, Parsing::ResourceScope scope, RootArg::Arg_t&& arg)
{
	// This switch case is required because compiler needs to know which version of _AddRootArg template to
	// generate during compile time.
	switch (*pass.input)
	{
	case Parsing::PassInputType::UI:
		_AddRootArg<Parsing::PassInputType::UI>(pass, frameGraph.passesGlobalRes, frameGraph.objGlobalResTemplate,
			updateFrequency, scope, std::move(arg));
		break;
	case Parsing::PassInputType::Static:
		_AddRootArg<Parsing::PassInputType::Static>(pass, frameGraph.passesGlobalRes, frameGraph.objGlobalResTemplate,
			updateFrequency, scope, std::move(arg));
		break;
	case Parsing::PassInputType::Dynamic:
		_AddRootArg<Parsing::PassInputType::Dynamic>(pass, frameGraph.passesGlobalRes, frameGraph.objGlobalResTemplate,
			updateFrequency, scope, std::move(arg));
		break;
	case Parsing::PassInputType::Particles:
		_AddRootArg<Parsing::PassInputType::Particles>(pass, frameGraph.passesGlobalRes, frameGraph.objGlobalResTemplate,
			updateFrequency, scope, std::move(arg));
		break;
	case Parsing::PassInputType::PostProcess:
		_AddRootArg<Parsing::PassInputType::PostProcess>(pass, frameGraph.passesGlobalRes, frameGraph.objGlobalResTemplate,
			updateFrequency, scope, std::move(arg));
		break;
	case Parsing::PassInputType::Debug:
		_AddRootArg<Parsing::PassInputType::Debug>(pass, frameGraph.passesGlobalRes, frameGraph.objGlobalResTemplate,
			updateFrequency, scope, std::move(arg));
		break;
	default:
		DX_ASSERT(false && "Unknown pass input for adding root argument");
		break;
	}
}


void FrameGraphBuilder::ValidatePassResources(const std::vector<PassParametersSource>& passesParametersSources) const
{
#ifdef ENABLE_VALIDATION

	// Per object resources are a bit special. From logical point of view it is totally fine if
	// global per object resources will collide if they are related to different types of objects,
	// so that's the reason I handle those types of resources different then pass resources.

	 std::array<std::vector<Parsing::Resource_t>, 
		 static_cast<int>(Parsing::PassInputType::SIZE)> perObjectGlobalResources;

	 std::vector<Parsing::Resource_t> perPassGlobalResources;

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

				// There should be no collision in local scope
				DX_ASSERT(count == 1 && "Name collision inside pass resource declaration");
			}

			// Global pass collision check
			{
				// Check if we have resource with the same name
				const auto resIt = std::find_if(perPassGlobalResources.cbegin(), perPassGlobalResources.cend(),
					[currentResName](const Parsing::Resource_t& res)
				{
					return currentResName == Parsing::GetResourceName(res);
				});


				if (Parsing::GetResourceScope(currentRes) == Parsing::ResourceScope::Global &&
					Parsing::GetResourceBindFrequency(currentRes) == Parsing::ResourceBindFrequency::PerPass)
				{
					// Handle properly when it might be resource of the same type we are checking collision against

					if (resIt != perPassGlobalResources.cend())
					{
						// Make sure content is equal. If yes, then this is just the same resource,
						// if no then we have name collision
						DX_ASSERT(Parsing::IsEqual(*resIt, currentRes) && "Global resource name collision is found");
					}
					else
					{
						// No such resource were found. Add this one to the list
						perPassGlobalResources.push_back(currentRes);
					}
				}
				else
				{
					DX_ASSERT(resIt == perPassGlobalResources.cend() && "Global resource name collision is found");
				}
				
			}

			// Global per object collision check
			{
				if (Parsing::GetResourceBindFrequency(currentRes) == Parsing::ResourceBindFrequency::PerObject)
				{
					// As I said before for global per object we only need to check against resources of the same input type

					std::vector<Parsing::Resource_t>& objTypeGlobalResource =
						perObjectGlobalResources[static_cast<int>(*paramSource.input)];

					// Check if we have resource with the same name
					const auto resIt = std::find_if(objTypeGlobalResource.cbegin(), objTypeGlobalResource.cend(),
						[currentResName](const Parsing::Resource_t& res)
					{
						return currentResName == Parsing::GetResourceName(res);
					});

					
					if (Parsing::GetResourceScope(currentRes) == Parsing::ResourceScope::Global)
					{
						if (resIt != objTypeGlobalResource.cend())
						{
							// Make sure content is equal. If yes, then this is just the same resource,
							// if no then we have name collision
							DX_ASSERT(Parsing::IsEqual(*resIt, currentRes) && "Global resource name collision is found");
						}
						else
						{
							// No such resource were found. Add this one to the list
							objTypeGlobalResource.push_back(currentRes);
						}
					}
					else
					{
						DX_ASSERT(resIt == objTypeGlobalResource.cend() && "Global resource name collision is found");
					}
					
				}
				else
				{
					for (std::vector<Parsing::Resource_t>& objTypeGlobalResource : perObjectGlobalResources)
					{
						// PerPass resources should not collide with any PerObject resource

						// Check if we have resource with the same name
						const auto resIt = std::find_if(objTypeGlobalResource.cbegin(), objTypeGlobalResource.cend(),
							[currentResName](const Parsing::Resource_t& res)
						{
							return currentResName == Parsing::GetResourceName(res);
						});


						// No need to add anything, this case was handled above
						DX_ASSERT(resIt == objTypeGlobalResource.cend() && "Global resource name collision is found");
					}
				}

				
			}

		}
	}

#endif
}

void FrameGraphBuilder::AttachSpecialPostPreCallbacks(std::vector<PassTask>& passTasks) const
{
	DX_ASSERT(passTasks.empty() == false && "AttachPostPreCallbacks failed. No pass tasks");

	for (PassTask& passTask : passTasks)
	{
		// Watch callbacks order here
		if (PassUtils::GetPassInputType(passTask.pass) != Parsing::PassInputType::PostProcess)
		{
			passTask.prePassCallbacks.insert(
				passTask.prePassCallbacks.end(),
				{ std::bind(PassUtils::RenderTargetToRenderStateCallback, std::placeholders::_1, std::placeholders::_2),
					std::bind(PassUtils::DepthTargetToRenderStateCallback, std::placeholders::_1, std::placeholders::_2)}
			);
		}

		
		passTask.postPassCallbacks.push_back(PassUtils::InternalTextureProxiesToInterPassStateCallback);
	}

	// End frame routine
	passTasks.back().postPassCallbacks.push_back(PassUtils::BackBufferToPresentStateCallback);
}

std::vector<FrameGraphBuilder::CompiledShaderData> FrameGraphBuilder::CompileShaders(const PassParametersSource& pass) const
{
	std::vector<FrameGraphBuilder::CompiledShaderData> passCompiledShaders;

	for (const PassParametersSource::ShaderSource& shader : pass.shaders)
	{
		std::string shaderDefsToInclude;

		// Add External Resources
		for (const std::string& externalDefName : shader.externals)
		{
			// Find resource and stub it into shader source

			// Holy C++ magic
			// What is it calls this function on a few containers listed below 
			bool result = std::invoke([&shaderDefsToInclude, &externalDefName, &pass](auto... shaderDefs) 
			{
				int shaderDefsToIncludeOldSize = shaderDefsToInclude.size();

				((
					std::for_each(shaderDefs->cbegin(), shaderDefs->cend(), [&shaderDefsToInclude, &externalDefName, &pass](const auto& def) 
				{
					using T = std::decay_t<decltype(def)>;

					if constexpr (std::is_same_v<T, Parsing::Resource_t>)
					{
						if (externalDefName == Parsing::GetResourceName(def))
						{
							shaderDefsToInclude += Parsing::GetResourceRawView(def);
						}
					}
					else if constexpr (std::is_same_v<T, Parsing::MiscDef_t>)
					{
						if (Parsing::GetMiscDefName(def) == externalDefName)
						{
							shaderDefsToInclude += Parsing::GetMiscDefRawView(def);
						}
					}
					else
					{
						if (externalDefName == def.name)
						{
							shaderDefsToInclude += def.rawView;
						}
					}

				}
				)), ...);

				// If the size of the string to include changed, then we found something
				return shaderDefsToIncludeOldSize != shaderDefsToInclude.size();

			}, // This is containers that this function is invoked on
				&pass.resources, &pass.vertAttr, &pass.functions, &pass.miscDefs);

			DX_ASSERT(result == true && "Some external shader resource was not found");

			shaderDefsToInclude += ";";
		}

		std::string sourceCode =
			shaderDefsToInclude +
			shader.source;

		// Got final shader source, now compile
		CompiledShaderData& compiledShader = passCompiledShaders.emplace_back(CompiledShaderData());
		compiledShader.type = shader.type;

		ComPtr<ID3DBlob> errors;

		const std::string strShaderType = PassParametersSource::ShaderTypeToStr(shader.type);

		Logs::Logf(Logs::Category::Parser, "Shader compilation, type: %s", strShaderType.c_str());

		HRESULT hr = D3DCompile(
			sourceCode.c_str(),
			sourceCode.size(),
			(pass.name + strShaderType).c_str(),
			nullptr,
			nullptr,
			"main",
			(Utils::StrToLower(strShaderType) + Settings::SHADER_FEATURE_LEVEL).c_str(),
			Settings::SHADER_COMPILATION_FLAGS,
			0,
			&compiledShader.shaderBlob,
			&errors
		);

		if (errors != nullptr)
		{
			Logs::Logf(Logs::Category::Parser, "Shader compilation error: %s",
				reinterpret_cast<char*>(errors->GetBufferPointer()));
		}

		ThrowIfFailed(hr);

		// Root signature is compiled in separate blob, it should be defined as #define in order for 
		// D3DCompile to accept it 
		const std::string rootSigDefine = "ROOT_SIGNATURE";
		std::string rawRootSig = "#define " + rootSigDefine + " \" " + pass.rootSignature->rawView + " \" ";

		hr = D3DCompile(
			rawRootSig.c_str(),
			rawRootSig.size(),
			("RootSignature_" + pass.name).c_str(),
			nullptr,
			nullptr,
			rootSigDefine.c_str(),
			"rootsig_1_1",
			0,
			0,
			&compiledShader.rootSigBlob,
			&errors
		);

		if (errors != nullptr)
		{
			Logs::Logf(Logs::Category::Parser, "Root signature compilation error: %s",
				reinterpret_cast<char*>(errors->GetBufferPointer()));
		}

		ThrowIfFailed(hr);
	}

	return passCompiledShaders;
}

void FrameGraphBuilder::BuildFrameGraph(std::unique_ptr<FrameGraph>& outFrameGraph, std::vector<FrameGraphSource::FrameGraphResourceDecl>& internalResourceDecl)
{
	FrameGraphSource source = GenerateFrameGraphSource();

	internalResourceDecl = std::move(source.resourceDeclarations);
	outFrameGraph = std::make_unique<FrameGraph>(CompileFrameGraph(std::move(source)));	
}

void FrameGraphBuilder::CreateFrameGraphResources(const std::vector<FrameGraphSource::FrameGraphResourceDecl>& resourceDecls, FrameGraph& frameGraph) const
{
	frameGraph.internalTextureNames = std::make_shared<std::vector<std::string>>(CreateFrameGraphResources(resourceDecls));
}

FrameGraph FrameGraphBuilder::CompileFrameGraph(FrameGraphSource&& source) const
{
	Logs::Log(Logs::Category::Parser, "CompileFrameGraph start");

	FrameGraph frameGraph;

	ValidatePassResources(source.passesParametersSources);
	
	std::vector<PassTask::Callback_t> pendingCallbacks;

	// Add passes to frame graph in proper order
	for (const FrameGraphSource::Step_t& step : source.steps)
	{
		std::visit([&frameGraph, &source, &pendingCallbacks, this](auto&& step) 
		{
			using T = std::decay_t<decltype(step)>;

			if constexpr (std::is_same_v<T, FrameGraphSource::Pass>)
			{
				const std::string& passName = step.name;

				Logs::Logf(Logs::Category::Parser, "Compile pass, start: %s", passName.c_str());

				auto passParamIt = std::find_if(source.passesParametersSources.begin(), source.passesParametersSources.end(),
					[&passName](const PassParametersSource& paramSource)
				{
					return paramSource.name == passName;
				});

				DX_ASSERT(passParamIt != source.passesParametersSources.end() && "Pass source described in framegraph is not found");

				// Ugly hack to save data before they will be moved
				std::vector<PassParametersSource::FixedFunction_t> prePassFuncs = passParamIt->prePassFuncs;
				std::vector<PassParametersSource::FixedFunction_t> postPassFuncs = passParamIt->postPassFuncs;

				PassParameters passParam = CompilePassParameters(std::move(*passParamIt), frameGraph);

				// Add pass
				switch (*passParam.input)
				{
				case Parsing::PassInputType::UI:
				{
					frameGraph.passTasks.emplace_back(PassTask{ Pass_UI{} });
				}
				break;
				case Parsing::PassInputType::Static:
				{
					frameGraph.passTasks.emplace_back(PassTask{ Pass_Static{} });
				}
				break;
				case Parsing::PassInputType::Dynamic:
				{
					frameGraph.passTasks.emplace_back(PassTask{ Pass_Dynamic{} });
				}
				break;
				case Parsing::PassInputType::Particles:
				{
					frameGraph.passTasks.emplace_back(PassTask{ Pass_Particles{} });
				}
				break;
				case Parsing::PassInputType::PostProcess:
				{
					frameGraph.passTasks.emplace_back(PassTask{ Pass_PostProcess{} });
				}
				break;
				case Parsing::PassInputType::Debug:
				{
					frameGraph.passTasks.emplace_back(PassTask{ Pass_Debug{} });
				}
				break;
				default:
					DX_ASSERT(false && "Pass with undefined input is detected");
					break;
				}

				PassTask& currentPassTask = frameGraph.passTasks.back();
				currentPassTask.prePassCallbacks = CompilePassCallbacks(prePassFuncs, passParam);
				currentPassTask.postPassCallbacks = CompilePassCallbacks(postPassFuncs, passParam);

				if (pendingCallbacks.empty() == false)
				{
					currentPassTask.prePassCallbacks.insert(
						currentPassTask.prePassCallbacks.end(),
						pendingCallbacks.begin(),
						pendingCallbacks.end());

					pendingCallbacks.clear();
				}

				// Set up pass parameters
				std::visit([&passParam](auto&& pass)
				{
					pass.passParameters = std::move(passParam);

				}, currentPassTask.pass);
				
			}
			
			if constexpr (std::is_same_v<T, FrameGraphSource::FixedFunctionCopy>)
			{
				PassTask::Callback_t copyCallback = std::bind(
					PassUtils::CopyTextureCallback,
					step.source, step.destination, std::placeholders::_1, std::placeholders::_2);

				if (frameGraph.passTasks.empty())
				{
					pendingCallbacks.push_back(copyCallback);
				}
				else
				{
					frameGraph.passTasks.back().postPassCallbacks.push_back(copyCallback);
				}
			}

		}, step);
	}

	AttachSpecialPostPreCallbacks(frameGraph.passTasks);

	return frameGraph;
}

FrameGraphSource FrameGraphBuilder::GenerateFrameGraphSource() const
{
	FrameGraphSource frameGraphSource;

	std::shared_ptr<Parsing::FrameGraphSourceContext> parseCtx = ParseFrameGraphFile(LoadFrameGraphFile());

	frameGraphSource.passesParametersSources = GeneratePassesParameterSources(*parseCtx);

	frameGraphSource.steps = std::move(parseCtx->steps);
	frameGraphSource.resourceDeclarations = std::move(parseCtx->resources);

	return frameGraphSource;
}

std::vector<std::string> FrameGraphBuilder::CreateFrameGraphResources(const std::vector<FrameGraphSource::FrameGraphResourceDecl>& resourceDecls) const
{
	ResourceManager& resourceManager = ResourceManager::Inst();
	Frame& frame = Renderer::Inst().GetMainThreadFrame();

	std::vector<std::string> internalResourcesName;
	internalResourcesName.reserve(resourceDecls.size());

	for (const FrameGraphSource::FrameGraphResourceDecl& resourceDecl : resourceDecls)
	{
		const std::string& resourceName = internalResourcesName.emplace_back(resourceDecl.name);
		
		DX_ASSERT(resourceManager.FindResource(resourceName) == nullptr && "This framegraph resource already exists");

		DX_ASSERT(resourceDecl.desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && 
			"Only 2D textures are implemented as frame graph internal res");

		ResourceDesc desc;
		desc.width = resourceDecl.desc.Width;
		desc.height = resourceDecl.desc.Height;
		desc.format = resourceDecl.desc.Format;
		desc.flags = resourceDecl.desc.Flags;
		desc.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		FArg::CreateTextureFromDataDeferred createTexArgs;
		createTexArgs.data = nullptr;
		createTexArgs.desc = &desc;
		createTexArgs.name = resourceName.c_str();
		createTexArgs.frame = &frame;
		createTexArgs.clearValue = resourceDecl.clearValue.has_value() ? &resourceDecl.clearValue.value() : nullptr;
		createTexArgs.saveResourceInCPUMemory = false;

		std::vector<std::byte> initBuffer;
		if (resourceDecl.initValue.has_value() == true)
		{
			const int elementsNum = desc.width * desc.height;

			// We need to init this resource. Create temp buffer and populate it with whatever
			switch (desc.format)
			{
			case DXGI_FORMAT_R32G32B32A32_FLOAT:
			{
				initBuffer = CreateInitBuffer<float, 4>(elementsNum, &resourceDecl.initValue->x);
			}
			break;
			case DXGI_FORMAT_R8G8B8A8_UNORM:
			{
				constexpr int componentsNum = 4;

				constexpr uint8_t maxVal = std::numeric_limits<uint8_t>::max();

				DX_ASSERT(resourceDecl.initValue->x >= 0.0f && resourceDecl.initValue->x <= 1.0f && "Normalized value is not in range");
				DX_ASSERT(resourceDecl.initValue->y >= 0.0f && resourceDecl.initValue->y <= 1.0f && "Normalized value is not in range");
				DX_ASSERT(resourceDecl.initValue->z >= 0.0f && resourceDecl.initValue->z <= 1.0f && "Normalized value is not in range");
				DX_ASSERT(resourceDecl.initValue->w >= 0.0f && resourceDecl.initValue->w <= 1.0f && "Normalized value is not in range");

				uint8_t initValue[componentsNum] = {
					static_cast<uint8_t>(maxVal * resourceDecl.initValue->x),
					static_cast<uint8_t>(maxVal * resourceDecl.initValue->y),
					static_cast<uint8_t>(maxVal * resourceDecl.initValue->z),
					static_cast<uint8_t>(maxVal * resourceDecl.initValue->w)
				};

				initBuffer = CreateInitBuffer<uint8_t, componentsNum>(elementsNum, initValue);
			}
			break;
			case DXGI_FORMAT_D24_UNORM_S8_UINT: 
			{
				constexpr int componentsNum = 1;

				// Get max for 24 unsigned integer
				constexpr uint32_t u24maxVal = std::numeric_limits<uint32_t>::max() & (0xFFFFFF);

				DX_ASSERT(resourceDecl.initValue->x >= 0.0f && resourceDecl.initValue->x <= 1.0f && "Normalized value is not in range");

				const uint32_t u24Val = static_cast<uint32_t>(resourceDecl.initValue->x * u24maxVal);
				const uint8_t u8Val = static_cast<uint8_t>(resourceDecl.initValue->y);

				uint32_t initValue[componentsNum] = { u24Val | (u8Val << 24) };

				initBuffer = CreateInitBuffer<uint32_t, componentsNum>(elementsNum, initValue);
			}
			break;
			default:
				DX_ASSERT(false && "Invalid init format, probably not implemented yet");
				break;
			}

			createTexArgs.data = initBuffer.data();
		}

		resourceManager.CreateTextureFromDataDeferred(createTexArgs);
	}

	return internalResourcesName;
}

std::vector<ResourceProxy> FrameGraphBuilder::CreateFrameGraphTextureProxies(const std::vector<std::string>& internalTextureList) const
{
	std::vector<ResourceProxy> proxies;

	ResourceManager& resMan = ResourceManager::Inst();

	for(const std::string& textureName : internalTextureList)
	{
		Resource* texture = resMan.FindResource(textureName);

		DX_ASSERT(texture != nullptr && "Failed to create texture proxy. No such texture is found");

		ResourceProxy& newProxy = proxies.emplace_back(ResourceProxy{ *texture->gpuBuffer.Get() });
		newProxy.hashedName = HASH(texture->name.c_str());
	}

	return proxies;
}

std::vector<PassParametersSource> FrameGraphBuilder::GeneratePassesParameterSources(const Parsing::FrameGraphSourceContext& frameGraphContext) const
{
	std::unordered_map<std::string, std::string> passSourceFiles = LoadPassFiles();
	
	std::shared_ptr<Parsing::PreprocessorContext> preprocessCtx = ParsePreprocessPassFiles(passSourceFiles);
	// Preprocessing is currently applied only to pass files, so there is no need to check that there is no
	// nested includes. However, if I would decide to apply preprocessing to other random file it would be
	// critical to either implement some kind of validation or actually made #include to work in nested manner
	PreprocessPassFiles(passSourceFiles, *preprocessCtx);

	std::shared_ptr<Parsing::PassParametersContext> parseCtx = ParsePassFiles(passSourceFiles, frameGraphContext);

	std::vector<PassParametersSource> passesParametersSources;

	for (PassParametersSource& passParameterSource : parseCtx->passSources)
	{
		passesParametersSources.push_back(std::move(passParameterSource));
	}

	PostprocessPassesParameterSources(passesParametersSources);

	return passesParametersSources;
}

void FrameGraphBuilder::PostprocessPassesParameterSources(std::vector<PassParametersSource>& parameters) const
{
	for (PassParametersSource& source : parameters)
	{
		for (Parsing::Resource_t& resource : source.resources)
		{
			std::visit([](auto&& res)
			{
				using T = std::decay_t<decltype(res)>;

				if constexpr (std::is_same_v<T, Parsing::Resource_ConstBuff>)
				{
					AlignConstBufferFields(res);
				}

			}, resource);
		}
	}
}

std::unordered_map<std::string, std::string> FrameGraphBuilder::LoadPassFiles() const
{
	std::unordered_map<std::string, std::string> passFiles;

	for (const auto& file : std::filesystem::directory_iterator(Utils::GenAbsolutePathToFile(Settings::FRAMEGRAPH_DIR)))
	{
		const std::filesystem::path filePath = file.path();

		if (filePath.extension() == Settings::FRAMEGRAPH_PASS_FILE_EXT)
		{
			Logs::Logf(Logs::Category::Parser, "Read pass file %s", filePath.c_str());

			std::string passFileContent = Utils::ReadFile(filePath);

			passFiles.emplace(std::make_pair(
				filePath.filename().string(),
				std::move(passFileContent)));
		}
	}

	return passFiles;
}

std::string FrameGraphBuilder::LoadFrameGraphFile() const
{
	for (const auto& file : std::filesystem::directory_iterator(Utils::GenAbsolutePathToFile(Settings::FRAMEGRAPH_DIR)))
	{
		const std::filesystem::path filePath = file.path();

		if (filePath.extension() == Settings::FRAMEGRAPH_FILE_EXT)
		{
			Logs::Logf(Logs::Category::Parser, "Read frame graph file %s", filePath.c_str());

			std::string frameGraphFileContent = Utils::ReadFile(filePath);

			return frameGraphFileContent;
		}
	}

	DX_ASSERT(false && "Material file was not found");

	return std::string();
}

std::shared_ptr<Parsing::PreprocessorContext> FrameGraphBuilder::ParsePreprocessPassFiles(const std::unordered_map<std::string, std::string>& passFiles) const
{
	peg::parser parser;
	InitPreprocessorParser(parser);

	std::shared_ptr<Parsing::PreprocessorContext> context = std::make_shared<Parsing::PreprocessorContext>();

	for (const auto& passFile : passFiles)
	{
		context->currentFile = passFile.first;
		context->includes[context->currentFile] = std::vector<Parsing::PreprocessorContext::Include>{};

		Logs::Logf(Logs::Category::Parser, "Preprocess pass file, start: %s", passFile.first.c_str());

		std::any ctx = context;

		parser.parse(passFile.second.c_str(), ctx);
	}

	return context;
}

std::shared_ptr<Parsing::PassParametersContext> FrameGraphBuilder::ParsePassFiles(const std::unordered_map<std::string, std::string>& passFiles, 
	const Parsing::FrameGraphSourceContext& frameGraphContext) const
{
	peg::parser parser;
	InitPassParser(parser);

	std::shared_ptr<Parsing::PassParametersContext> context = std::make_shared<Parsing::PassParametersContext>();
	context->frameGraphContext = &frameGraphContext;

	for (const auto& passFile : passFiles)
	{
		context->passSources.emplace_back(PassParametersSource()).name = passFile.first.substr(0, passFile.first.rfind('.'));

		Logs::Logf(Logs::Category::Parser, "Parse pass file, start: %s", context->passSources.back().name.c_str());

		std::any ctx = context;

		parser.parse(passFile.second.c_str(), ctx);
	}

	return context;
}

std::shared_ptr<Parsing::FrameGraphSourceContext> FrameGraphBuilder::ParseFrameGraphFile(const std::string& frameGraphSourceFileContent) const
{
	peg::parser parser;
	InitFrameGraphParser(parser);

	std::shared_ptr<Parsing::FrameGraphSourceContext> context = std::make_shared<Parsing::FrameGraphSourceContext>();
	std::any ctx = context;

	Logs::Log(Logs::Category::Parser, "Parse frame graph file, start");

	parser.parse(frameGraphSourceFileContent.c_str(), ctx);

	return context;
}

bool FrameGraphBuilder::IsSourceChanged()
{
	if (sourceWatchHandle == INVALID_HANDLE_VALUE)
	{
		// First time requested. Init handler
		sourceWatchHandle = FindFirstChangeNotification(Utils::GetAbsolutePathToRootDir().string().c_str(), TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME |
			FILE_NOTIFY_CHANGE_DIR_NAME |
			FILE_NOTIFY_CHANGE_LAST_WRITE);

		DX_ASSERT(sourceWatchHandle != INVALID_HANDLE_VALUE && "Failed to init source watch handle");

		return true;
	}

	// Time out value for wait is 0, so the function will return immediately and no actual wait happens
	const DWORD waitStatus = WaitForSingleObject(sourceWatchHandle, 0);

	DX_ASSERT(waitStatus == WAIT_OBJECT_0 || waitStatus == WAIT_TIMEOUT && "IsSourceChange failed. Wait function returned unexpected result");

	if (waitStatus == WAIT_OBJECT_0)
	{
		// Object was signaled, set up next wait
		BOOL res = FindNextChangeNotification(sourceWatchHandle);
		DX_ASSERT(res == TRUE && "Failed to set up next change notification, for source watch");

		return true;
	}

	return false;
}

void FrameGraphBuilder::PreprocessPassFiles(std::unordered_map<std::string, std::string>& passFiles, Parsing::PreprocessorContext& context) const
{
	for (auto& fileInclude : context.includes)
	{
		// Sort includes first
		std::sort(fileInclude.second.begin(), fileInclude.second.end(), []
		(Parsing::PreprocessorContext::Include& rv,   Parsing::PreprocessorContext::Include& lv) 
		{
			return rv.pos < lv.pos;
		});

		std::string& currentFile = passFiles[fileInclude.first];

		std::string processedFile;
		processedFile.reserve(currentFile.size());

		int currentPos = 0;

		for (const Parsing::PreprocessorContext::Include& include : fileInclude.second)
		{
			// Add chunk before this include
			processedFile += currentFile.substr(currentPos, include.pos - currentPos);
			currentPos = include.pos + include.len;

			// Add included file
			processedFile += Utils::ReadFile(Utils::GenAbsolutePathToFile(Settings::FRAMEGRAPH_DIR + "/" + include.name));
		}

		DX_ASSERT(currentPos < currentFile.size() && "PreprocessPassFile, something wrong with current pos");

		// Include last piece of the file
		if (currentPos + 1 != currentFile.size())
		{
			processedFile += currentFile.substr(currentPos);
		}

		currentFile = processedFile;
	}
}

std::vector<D3D12_INPUT_ELEMENT_DESC> FrameGraphBuilder::GenerateInputLayout(const PassParametersSource& pass) const
{
	const Parsing::VertAttr& vertAttr = *GetPassInputVertAttr(pass);

	DX_ASSERT(GetPassInputVertAttr(pass) != nullptr && "Can't generate input layout, no input vert attr is found");

	DX_ASSERT((pass.vertAttrSlots.empty() || pass.vertAttrSlots.size() == vertAttr.content.size())
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

const Parsing::VertAttr* FrameGraphBuilder::GetPassInputVertAttr(const PassParametersSource& pass) const
{
	if (*pass.input == Parsing::PassInputType::PostProcess)
	{
		return nullptr;
	}

	const std::string& inputName = pass.inputVertAttr;

	const auto attrIt = std::find_if(pass.vertAttr.cbegin(), pass.vertAttr.cend(),
		[inputName](const Parsing::VertAttr& attr) { return inputName == attr.name; });

	DX_ASSERT(attrIt != pass.vertAttr.cend() && "Can't find input vert attribute");

	return &(*attrIt);
}

ComPtr<ID3D12RootSignature> FrameGraphBuilder::GenerateRootSignature(const PassParametersSource& pass, const std::vector<FrameGraphBuilder::CompiledShaderData>& shaders) const
{
	Logs::Logf(Logs::Category::Parser, "GenerateRootSignature, start, pass: %s", pass.name.c_str());

	DX_ASSERT(shaders.empty() == false && "Can't generate root signature with not shaders");

	ComPtr<ID3D12RootSignature> rootSig;

	const ComPtr<ID3DBlob>& rootSigBlob = shaders.front().rootSigBlob;

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSig.GetAddressOf())));

	Diagnostics::SetResourceName(rootSig.Get(), std::string("Root sig, pass: ") + pass.name);

	return rootSig;
}

ComPtr<ID3D12PipelineState> FrameGraphBuilder::GeneratePipelineStateObject(const PassParametersSource& passSource, std::vector<FrameGraphBuilder::CompiledShaderData>& shaders, ComPtr<ID3D12RootSignature>& rootSig) const
{
	Logs::Logf(Logs::Category::Parser, "GeneratePipelineStateObject, start, pass %s", passSource.name.c_str());

	DX_ASSERT(passSource.input.has_value() == true && "Can't generate pipeline state object. Pass type is undefined ");

	switch (*passSource.input)
	{
	case Parsing::PassInputType::Static:
	case Parsing::PassInputType::Dynamic:
	case Parsing::PassInputType::Particles:
	case Parsing::PassInputType::UI:
	case Parsing::PassInputType::Debug:
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = passSource.rasterPsoDesc;

		// Set up render targets
		psoDesc.NumRenderTargets = passSource.colorTargetNames.size();
		
		for (int i = 0; i < passSource.colorTargetFormats.size(); ++i)
		{
			psoDesc.RTVFormats[i] = passSource.colorTargetFormats[i];
		}

		// Set up root sig
		psoDesc.pRootSignature = rootSig.Get();

		// Set up shaders
		for (const CompiledShaderData& shader : shaders)
		{
			DX_ASSERT(shader.type.has_value() == true);

			D3D12_SHADER_BYTECODE shaderByteCode = {
				reinterpret_cast<BYTE*>(shader.shaderBlob->GetBufferPointer()),
				shader.shaderBlob->GetBufferSize()
			};

			switch (*shader.type)
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
				DX_ASSERT(false && "Can't generate pipeline state object. Invalid shader type");
				break;
			}
		}

		// Set up input layout
		std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = GenerateInputLayout(passSource);
		psoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };

		ComPtr<ID3D12PipelineState> pipelineState;

		ThrowIfFailed(Infr::Inst().GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

		Diagnostics::SetResourceName(pipelineState.Get(), std::string("Raster PSO, pass: ") + passSource.name);

		return pipelineState;
	}
	case Parsing::PassInputType::PostProcess:
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = passSource.computePsoDesc;

		// Set up root sig
		psoDesc.pRootSignature = rootSig.Get();

		auto shaderIt = std::find_if(shaders.begin(), shaders.end(), [](const CompiledShaderData& shader)
		{
			return *shader.type == PassParametersSource::Cs;
		});

		DX_ASSERT(shaderIt != shaders.end() && "Can't generate compute pipeline state object, shader is not found");

		psoDesc.CS = {
				reinterpret_cast<BYTE*>(shaderIt->shaderBlob->GetBufferPointer()),
				shaderIt->shaderBlob->GetBufferSize()
		};

		ComPtr<ID3D12PipelineState> pipelineState;

		ThrowIfFailed(Infr::Inst().GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

		Diagnostics::SetResourceName(pipelineState.Get(), std::string("Compute PSO, pass: ") + passSource.name);

		return pipelineState;
	}
	default:
		DX_ASSERT(false && "Can't generate pipeline state object, unknown type");
		break;
	}

	return ComPtr<ID3D12PipelineState>();
}

void FrameGraphBuilder::CreateResourceArguments(const PassParametersSource& passSource, FrameGraph& frameGraph, PassParameters& pass) const
{
	Logs::Logf(Logs::Category::Parser, "CreateResourceArguments, start, pass: %s", passSource.name.c_str());

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

				DX_ASSERT(rootParam.num == 1 && "Inline const buffer view should always have numDescriptors 1");
				DX_ASSERT(res->bind.has_value() == false && "Internal bind for inline const buffer view is not implemented");

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
			else if constexpr (std::is_same_v<T, Parsing::RootParam_UAView>)
			{
				const Parsing::Resource_RWTexture* res =
					FindResourceOfTypeAndRegId<Parsing::Resource_RWTexture>(passResources, rootParam.registerId);

				DX_ASSERT(rootParam.num == 1 && "Inline UAV should always have numDescriptors 1");

				AddRootArg(pass,
					frameGraph,
					*res->bindFrequency,
					*res->scope,
					RootArg::UAView{
						paramIndex,
						HASH(res->name.c_str()),
						res->bind,
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

								SetScopeAndBindFrequency(bindFrequency, scope, res);
								
								DX_ASSERT(res->bind.has_value() == false && "Internal bind for const buffer view is not implemented");

								descTableArgument.content.emplace_back(RootArg::DescTableEntity_ConstBufferView{
									HASH(res->name.c_str()),
									res->content,
									Const::INVALID_BUFFER_HANDLER,
									Const::INVALID_INDEX
									});
							}
						}
						else if constexpr (std::is_same_v<T, Parsing::RootParam_ShaderResourceView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_Texture* resTexture =
									FindResourceOfTypeAndRegId<Parsing::Resource_Texture>(passResources, descTableParam.registerId + i);

								// Is it structured buffer or texture?
								if (resTexture != nullptr)
								{

									SetScopeAndBindFrequency(bindFrequency, scope, resTexture);

									descTableArgument.content.emplace_back(RootArg::DescTableEntity_Texture{
										HASH(resTexture->name.c_str()),
										resTexture->bind
										});
								}
								else
								{
									const Parsing::Resource_StructuredBuffer* resStructuredBuffer =
										FindResourceOfTypeAndRegId<Parsing::Resource_StructuredBuffer>(passResources, descTableParam.registerId + i);

									SetScopeAndBindFrequency(bindFrequency, scope, resStructuredBuffer);

									descTableArgument.content.emplace_back(RootArg::DescTableEntity_StructuredBufferView{
										HASH(resStructuredBuffer->name.c_str())
										});
								}
							}

						}
						else if constexpr (std::is_same_v<T, Parsing::RootParam_SamplerView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_Sampler* res =
									FindResourceOfTypeAndRegId<Parsing::Resource_Sampler>(passResources, descTableParam.registerId + i);

								SetScopeAndBindFrequency(bindFrequency, scope, res);

								DX_ASSERT(res->bind.has_value() == false && "Internal bind for sampler view is not implemented");

								descTableArgument.content.emplace_back(RootArg::DescTableEntity_Sampler{
									HASH(res->name.c_str())
									});
							}
						}
						else if constexpr (std::is_same_v<T, Parsing::RootParam_UAView>)
						{
							for (int i = 0; i < descTableParam.num; ++i)
							{
								const Parsing::Resource_RWTexture* res =
									FindResourceOfTypeAndRegId<Parsing::Resource_RWTexture>(passResources, descTableParam.registerId + i);
								
								SetScopeAndBindFrequency(bindFrequency, scope, res);

								descTableArgument.content.emplace_back(RootArg::DescTableEntity_UAView{
									HASH(res->name.c_str()),
									res->bind
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
			else if constexpr (std::is_same_v<T, Parsing::RootParam_ShaderResourceView>)
			{
				const Parsing::Resource_StructuredBuffer* res =
					FindResourceOfTypeAndRegId<Parsing::Resource_StructuredBuffer>(passResources, rootParam.registerId);

				DX_ASSERT(FindResourceOfTypeAndRegId<Parsing::Resource_Texture>(passResources, rootParam.registerId) == nullptr &&
					"Inline SRV descriptor cannot correspond to Texture");

				DX_ASSERT(rootParam.num == 1 && "Inline SRV should always have numDescriptors 1");
				DX_ASSERT(res->bind.has_value() == false && "Internal bind for inline SRV is not implemented");

				AddRootArg(pass,
					frameGraph,
					*res->bindFrequency,
					*res->scope,
					RootArg::StructuredBufferView{
						paramIndex,
						HASH(res->name.c_str())
					});

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
	passParam.threadGroups = passSource.threadGroups;
	passParam.name = passSource.name;
	passParam.primitiveTopology = passSource.primitiveTopology;
	passParam.viewport = passSource.viewport;
	passParam.depthRenderTarget = { passSource.depthTargetName, Const::INVALID_INDEX };

	passParam.colorRenderTargets.reserve(passSource.colorTargetNames.size());

	for (int i = 0; i < passSource.colorTargetNames.size(); ++i)
	{
		passParam.colorRenderTargets.push_back({ std::move(passSource.colorTargetNames[i]), Const::INVALID_INDEX });
	}

	if (const Parsing::VertAttr* inputVertAttr = GetPassInputVertAttr(passSource))
	{
		passParam.vertAttr = *inputVertAttr;
	}

	std::vector<FrameGraphBuilder::CompiledShaderData> compiledShaders = CompileShaders(passSource);
	passParam.rootSingature = GenerateRootSignature(passSource, compiledShaders);
	passParam.pipelineState = GeneratePipelineStateObject(passSource, compiledShaders, passParam.rootSingature);

	CreateResourceArguments(passSource, frameGraph, passParam);

	return passParam;
}

std::vector<PassTask::Callback_t> FrameGraphBuilder::CompilePassCallbacks(const std::vector<PassParametersSource::FixedFunction_t>& fixedFunctions, const PassParameters& passParams) const
{
	std::vector<PassTask::Callback_t> callbacks;

	for (const PassParametersSource::FixedFunction_t& fixedFunction : fixedFunctions)
	{
		std::visit([&callbacks, &passParams](auto&& fixedFunction) 
		{
			using T = std::decay_t<decltype(fixedFunction)>;

			if constexpr (std::is_same_v<T, PassParametersSource::FixedFunctionClearColor>)
			{
				callbacks.push_back(std::bind(PassUtils::ClearColorCallback, fixedFunction.color, std::placeholders::_1, std::placeholders::_2));
			}

			if constexpr (std::is_same_v<T, PassParametersSource::FixedFunctionClearDepth>)
			{
				callbacks.push_back(std::bind(PassUtils::ClearDeptCallback, fixedFunction.value, std::placeholders::_1,  std::placeholders::_2));
			}

		}, fixedFunction);
	}

	return callbacks;
}