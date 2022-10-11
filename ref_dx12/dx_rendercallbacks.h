#pragma once

#include <functional>

#include "Lib/crc32.h"
#include "dx_pass.h"
#include "dx_descriptorheapallocator.h"
#include "dx_resource.h"
#include "dx_app.h"
#include "dx_resourcemanager.h"
#include "dx_framegraph.h"
#include "dx_lightbaker.h"

// This is required because in some places template code will be generated for types where it's not intended.
#define DO_IF_SAME_DECAYED_TYPE(T1, T2, code) if constexpr (std::is_same_v<std::decay_t<T1>, std::decay_t<T2>>) { code; } \
	else { DX_ASSERT(false && "DO_IF_SAME_DECAYED_TYPE type failed in runtime" ); }

extern "C"
{
	#include "../client/ref.h"
};


namespace RenderCallbacks
{
	/*  Local, PerPass */

	struct UpdateGlobalPassContext
	{
		GPUJobContext& jobContext;
	};

	struct RegisterGlobalPassContext
	{
		GPUJobContext& jobContext;
	};

	struct UpdateGlobalObjectContext
	{
		XMFLOAT4X4 viewProjMat;
		GPUJobContext& jobContext;
	};

	struct RegisterGlobalObjectContext
	{
		GPUJobContext& jobContext;
	};

	struct RegisterLocalPassContext
	{
		GPUJobContext& jobContext;
	};

	struct UpdateLocalPassContext
	{
		GPUJobContext& jobContext;
	};

	struct RegisterLocalObjectContext
	{
		GPUJobContext& jobContext;
	};

	struct UpdateLocalObjectContext
	{
		GPUJobContext& jobContext;
	};

	/* Global */

	template<typename oT, typename bT>
	void RegisterGlobalObject(unsigned int paramName, oT& obj, bT& bindPoint, RegisterGlobalObjectContext& ctx)
	{
		using objT = std::decay_t<oT>;

		if constexpr(std::is_same_v<objT, DrawCall_UI_t>)
		{
			std::visit([&bindPoint, &ctx, paramName](auto&& obj) 
			{
				using uiDrawCallT = std::decay_t<decltype(obj)>;

				switch (paramName)
				{
				case HASH("gDiffuseMap"):
				{
					Renderer& renderer = Renderer::Inst();

					if constexpr (std::is_same_v< uiDrawCallT, DrawCall_Pic>)
					{
						std::array<char, MAX_QPATH> texFullName;
						ResourceManager::Inst().GetDrawTextureFullname(obj.name.c_str(), texFullName.data(), texFullName.size());

						Resource* tex = ResourceManager::Inst().FindOrCreateResource(texFullName.data(), ctx.jobContext);

						ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
						DO_IF_SAME_DECAYED_TYPE(bT, int, 
							renderer.cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->buffer.Get(), &emtpySrvDesc));

					}
					else
					{
						ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVTexture2DNullDescription();

						DO_IF_SAME_DECAYED_TYPE(bT, int, 
							renderer.cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
					}
				}
				break;
				default:
					break;
				}

			}, obj);
		} 
		else if constexpr(std::is_same_v<objT, StaticObject>)
		{
			switch (paramName)
			{
			case HASH("gDiffuseMap"):
			{
				Resource* tex = ResourceManager::Inst().FindResource(obj.textureKey.c_str());

				ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
				DO_IF_SAME_DECAYED_TYPE( bT, int, 
					Renderer::Inst().cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->buffer.Get(), &emtpySrvDesc));
			}
			break;
			default:
				break;
			}
		}
		else if constexpr(std::is_same_v<objT, entity_t>)
		{
			switch(paramName)
			{
			case HASH("gDiffuseMap"):
			{
				DX_ASSERT(obj.skin == nullptr && "Custom skin. I am not prepared for this");

				Renderer& renderer = Renderer::Inst();

				const DynamicObjectModel& model = renderer.GetDynamicModels().at(obj.model);

				Resource* tex = nullptr;

				if (obj.skinnum >= MAX_MD2SKINS)
				{
					tex = ResourceManager::Inst().FindResource(model.textures[0]);
				}
				else
				{
					tex = ResourceManager::Inst().FindResource(model.textures[obj.skinnum]);

					if (tex == nullptr)
					{
						tex = ResourceManager::Inst().FindResource(model.textures[0]);
					}
				}

				DX_ASSERT(tex != nullptr && "Not texture found for dynamic object rendering. Implement fall back");

				ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
				DO_IF_SAME_DECAYED_TYPE(bT, int, 
					renderer.cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->buffer.Get(), &emtpySrvDesc));
			}
			break;
			default:
				break;
			}
		}
		else if constexpr (std::is_same_v<objT, DebugObject_t>)
		{
		}
		else
		{
			DX_ASSERT(false && "RegisterGlobalObject unknown object type");
		}
	}

	template<typename oT, typename bT>
	void UpdateGlobalObject(unsigned int paramName, oT& obj, bT& bindPoint, UpdateGlobalObjectContext& ctx)
	{
		using objT = std::decay_t<oT>;

		if constexpr (std::is_same_v<objT, DrawCall_UI_t>)
		{
			std::visit([&bindPoint, &ctx, paramName](auto&& obj) 
			{
				using uiDrawVallT = std::decay_t<decltype(obj)>;

				switch (paramName)
				{
				case HASH("gWorldViewProj"):
				{
					XMMATRIX mvpMat = XMMatrixTranslation(obj.x, obj.y, 0.0f);

					XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), mvpMat * XMLoadFloat4x4(&ctx.viewProjMat));
				}
				break;
				case HASH("type"):
				{
					int& type = reinterpret_cast<int&>(bindPoint);

					if constexpr (std::is_same_v<uiDrawVallT, DrawCall_Pic>)
					{
						type = 0;
					}
					else if constexpr (std::is_same_v<uiDrawVallT, DrawCall_Char>)
					{
						type = 1;
					}
					else if constexpr (std::is_same_v<uiDrawVallT, DrawCall_StretchRaw>)
					{
						type = 2;
					}
					else
					{
						static_assert(false, "Invalid UI draw call type");
					}
				}
				break;
				default:
					break;
				}
			}, obj);

			
		}
		else if constexpr (std::is_same_v<objT, StaticObject>)
		{
			
		}
		else if constexpr (std::is_same_v<objT, entity_t>)
		{
			switch (paramName)
			{
			case HASH("gWorldViewProj"):
			{
				XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), 
					DynamicObjectModel::GenerateModelMat(obj) * ctx.jobContext.frame.camera.GetViewProjMatrix());
			}
			break;
			case HASH("gAnimMove"):
			{
				const DynamicObjectModel& model = Renderer::Inst().GetDynamicModels().at(obj.model);
				auto[animMove, frontLerp, backLerp] =  model.GenerateAnimInterpolationData(obj);

				reinterpret_cast<XMFLOAT4&>(bindPoint) = animMove;
			}
			break;
			case HASH("gFrontLerp"):
			{
				const DynamicObjectModel& model = Renderer::Inst().GetDynamicModels().at(obj.model);
				auto[animMove, frontLerp, backLerp] = model.GenerateAnimInterpolationData(obj);

				reinterpret_cast<XMFLOAT4&>(bindPoint) = frontLerp;
			}
			break;
			case HASH("gBackLerp"):
			{
				const DynamicObjectModel& model = Renderer::Inst().GetDynamicModels().at(obj.model);
				auto[animMove, frontLerp, backLerp] = model.GenerateAnimInterpolationData(obj);

				reinterpret_cast<XMFLOAT4&>(bindPoint) = backLerp;
			}
			break;
			default:
				break;
			}
		}
		else if constexpr (std::is_same_v<objT, DebugObject_t>)
		{
			switch (paramName)
			{
			case HASH("gDebugObjectType"):
			{
				std::visit([&bindPoint](auto&& object)
				{
					using T = std::decay_t<decltype(object)>;

					if constexpr (std::is_same_v<T, DebugObject_LightProbe>)
					{
						reinterpret_cast<int&>(bindPoint) = static_cast<int>(DebugObjectType::LightProbe);
					}
					else if constexpr (std::is_same_v<T, DebugObject_LightSource>)
					{
						reinterpret_cast<int&>(bindPoint) = static_cast<int>(DebugObjectType::LightSource);
					}
					else if constexpr (std::is_same_v<T, DebugObject_ProbePathSegment>)
					{
						reinterpret_cast<int&>(bindPoint) = static_cast<int>(DebugObjectType::ProbePathTraceSegment);
					}
					else if constexpr (std::is_same_v<T, DebugObject_ProbeLightSample>)
					{
						reinterpret_cast<int&>(bindPoint) = static_cast<int>(DebugObjectType::ProbeLightSample);
					}
					else
					{
						DX_ASSERT(false && "Unidentified debug object type");
					}

				}, obj);
			}
			break;
			default:
				break;
			}
		}
		else
		{
			DX_ASSERT(false && "UpdateGlobalObject unknown object type");
		}

	}


	template<typename DescT, typename bT>
	void RegisterInternalResource(bT& bindPoint, std::string_view internalResourceName)
	{
		// For internal resource there is no difference if it is global or local,
		// object or pass. No context passed as well because essentially those kind of
		// resources are handled on GPU side.
		Resource* tex = ResourceManager::Inst().FindResource(internalResourceName);

		DX_ASSERT(tex != nullptr && "Can register internal resource. Target texture doesn't exist");

		ViewDescription_t emtpySrvDesc;
		if constexpr( std::is_same_v<DescT,D3D12_SHADER_RESOURCE_VIEW_DESC>)
		{
			emtpySrvDesc = ViewDescription_t{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
		}

		if constexpr (std::is_same_v<DescT, D3D12_UNORDERED_ACCESS_VIEW_DESC>)
		{
			emtpySrvDesc = ViewDescription_t{ std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC>(std::nullopt) };
		}
		
		if constexpr (std::is_same_v<DescT, D3D12_CONSTANT_BUFFER_VIEW_DESC>)
		{
			DX_ASSERT(false && "Not implemented");
		}

		DO_IF_SAME_DECAYED_TYPE(bT, int, Renderer::Inst().cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->buffer.Get(), &emtpySrvDesc));
	}

	template<typename bT>
	void RegisterGlobalPass(unsigned int paramName, bT& bindPoint, RegisterGlobalPassContext& ctx)
	{
		switch (paramName)
		{
		case HASH("gFontTex"): 
		{
			std::array<char, MAX_QPATH> texFullName;
			ResourceManager& resMan = ResourceManager::Inst();

			resMan.GetDrawTextureFullname(Resource::FONT_TEXTURE_NAME, texFullName.data(), texFullName.size());

			Resource* tex = resMan.FindResource(texFullName.data());
			if (tex == nullptr)
			{
				tex = resMan.CreateTextureFromFile(texFullName.data(), ctx.jobContext);
			}

			ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
			DO_IF_SAME_DECAYED_TYPE(bT, int, Renderer::Inst().cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->buffer.Get(), &emtpySrvDesc));
		}
		break;
		case HASH("sbDiffuseProbes"):
		{
		}
		break;
		case HASH("ClusterAABBs"):
		{
		}
		break;
		case HASH("ClusterAABBsSize"):
		{
		}
		break;
		default:
			break;
		}
	}

	template<typename bT>
	void UpdateGlobalPass(unsigned int paramName, bT& bindPoint, UpdateGlobalPassContext& ctx)
	{
		switch (paramName)
		{
		case HASH("gMovieTex"):
		{
			Resource* tex = ResourceManager::Inst().FindResource(Resource::RAW_TEXTURE_NAME);
			DX_ASSERT(tex != nullptr && "Draw_RawPic texture doesn't exist");

			std::vector<DrawCall_UI_t>& uiDrawCalls = ctx.jobContext.frame.uiDrawCalls;

			auto movieDrawCallIt = std::find_if(uiDrawCalls.cbegin(), uiDrawCalls.cend(), [](const DrawCall_UI_t& drawCall)
			{
				return std::holds_alternative<DrawCall_StretchRaw>(drawCall);

			});

			if (movieDrawCallIt != uiDrawCalls.cend())
			{
				const DrawCall_StretchRaw& movieDrawCall = std::get<DrawCall_StretchRaw>(*movieDrawCallIt);

				// If there is no data, then texture is requested to be created for this frame. So no need to update
				if (movieDrawCall.data.empty() == false)
				{
					const int textureSize = movieDrawCall.textureWidth * movieDrawCall.textureHeight;

					CommandList& commandList = *ctx.jobContext.commandList;

					std::vector<unsigned int> texture(textureSize, 0);
					const std::array<unsigned int, 256>&  rawPalette = Renderer::Inst().GetRawPalette();

					for (int i = 0; i < textureSize; ++i)
					{
						texture[i] = rawPalette[std::to_integer<int>(movieDrawCall.data[i])];
					}

					ResourceManager::Inst().UpdateResource(*tex, reinterpret_cast<std::byte*>(texture.data()), ctx.jobContext);
				}

				ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, tex->buffer.Get(), &emtpySrvDesc));
			}
			else
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVTexture2DNullDescription();
				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
		}
		break;
		case HASH("gViewProj"):
		{
			XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), ctx.jobContext.frame.camera.GetViewProjMatrix());
		}
		break;
		case HASH("gCameraYaw"):
		{
			auto[yaw, pitch, roll] = ctx.jobContext.frame.camera.GetBasis();

			reinterpret_cast<XMFLOAT4&>(bindPoint) = yaw;
		}
		break;
		case HASH("gCameraPitch"):
		{
			auto[yaw, pitch, roll] = ctx.jobContext.frame.camera.GetBasis();

			reinterpret_cast<XMFLOAT4&>(bindPoint) = pitch;
		}
		break;
		case HASH("gCameraRoll"):
		{
			auto[yaw, pitch, roll] = ctx.jobContext.frame.camera.GetBasis();

			reinterpret_cast<XMFLOAT4&>(bindPoint) = roll;
		}
		break;
		case HASH("gCameraOrigin"):
		{
			reinterpret_cast<XMFLOAT4&>(bindPoint) = ctx.jobContext.frame.camera.position;
		}
		break;
		case HASH("sbDiffuseProbes"):
		{
			ResourceManager& resMan = ResourceManager::Inst();
			
			Renderer::Inst().TryTransferDiffuseIndirectLightingToGPU(ctx.jobContext);

			Resource* probeGpuBuffer = resMan.FindResource(Resource::PROBE_STRUCTURED_BUFFER_NAME);
			if (probeGpuBuffer == nullptr)
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVBufferNullDescription();
				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
			else
			{
				ViewDescription_t defaultSrvDesc{ 
					DescriptorHeapUtils::GenerateDefaultStructuredBufferViewDesc(probeGpuBuffer, sizeof(DiffuseProbe::DiffuseSH_t)) };

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, probeGpuBuffer->buffer.Get(), &defaultSrvDesc));
			}
		}
		break;
		case HASH("ClusterAABBs"):
		{
			// Should be the same as case above
			const std::string clusterAABBResourceName = "ClusterAABBs";

			ResourceManager& resMan = ResourceManager::Inst();

			// Check if this resource already exists
			if (resMan.FindResource(clusterAABBResourceName) == nullptr)
			{
				const BSPTree& bspTree = Renderer::Inst().GetBSPTree();
				const std::set<int> clusterSet = bspTree.GetClustersSet();

				// Check if we already have BSP loaded
				if (clusterSet.empty() == false)
				{
					std::vector<Utils::AABB> clusterAABB(clusterSet.size());

					std::transform(clusterSet.cbegin(), clusterSet.cend(), clusterAABB.begin(),
						[&bspTree](const int clusterIndex)
					{
						return bspTree.GetClusterAABB(clusterIndex);
					});

					ResourceDesc desc;
					desc.width = clusterAABB.size() * sizeof(Utils::AABB);
					desc.height = 1;
					desc.format = DXGI_FORMAT_UNKNOWN;
					desc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
					desc.flags = D3D12_RESOURCE_FLAG_NONE;

					FArg::CreateStructuredBuffer args;
					args.context = &ctx.jobContext;
					args.desc = &desc;
					args.data = reinterpret_cast<std::byte*>(clusterAABB.data());
					args.name = clusterAABBResourceName.c_str();

					resMan.CreateStructuredBuffer(args);
				}
			}

			if (Resource* clusterAABBBuffer = resMan.FindResource(clusterAABBResourceName))
			{
				ViewDescription_t defaultSrvDesc{
					DescriptorHeapUtils::GenerateDefaultStructuredBufferViewDesc(clusterAABBBuffer, sizeof(Utils::AABB)) };

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, clusterAABBBuffer->buffer.Get(), &defaultSrvDesc));
			}
			else
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVBufferNullDescription();

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}

		}
		break;
		case HASH("ClusterAABBsSize"):
		{
			const BSPTree& bspTree = Renderer::Inst().GetBSPTree();
			const std::set<int> clusterSet = bspTree.GetClustersSet();

			int& clusterAAABsSize = reinterpret_cast<int&>(bindPoint);
			clusterAAABsSize = clusterSet.size();
		}
		break;
		//#DEBUG I think when I regenerate data, resource like this one are not updated.
		// They created only once when stuff appears. This needs to be fixed. This also 
		// happens for ClusterAABB and etc (GO THROUGH PROBE RESOURCE AND SEE WHAT IS UP)
		case HASH("ClusterProbeGridInfoBuffer"):
		{
			// Should be the same as case above
			const std::string clusterProbeGridInfoResourceName = "ClusterProbeGridInfo";

			ResourceManager& resMan = ResourceManager::Inst();

			if (resMan.FindResource(clusterProbeGridInfoResourceName) == nullptr)
			{
				std::vector<ClusterProbeGridInfo> probeGridInfo = Renderer::Inst().GenBakeClusterProbeGridInfo();

				if (probeGridInfo.empty() == false)
				{
					ResourceDesc desc;
					desc.width = probeGridInfo.size() * sizeof(ClusterProbeGridInfo);
					desc.height = 1;
					desc.format = DXGI_FORMAT_UNKNOWN;
					desc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
					desc.flags = D3D12_RESOURCE_FLAG_NONE;

					FArg::CreateStructuredBuffer args;
					args.context = &ctx.jobContext;
					args.desc = &desc;
					args.data = reinterpret_cast<std::byte*>(probeGridInfo.data());
					args.name = clusterProbeGridInfoResourceName.c_str();

					resMan.CreateStructuredBuffer(args);
				}
			}

			if (Resource* clusterProbeGridInfoBuffer = resMan.FindResource(clusterProbeGridInfoResourceName))
			{
				ViewDescription_t defaultSrvDesc{
					DescriptorHeapUtils::GenerateDefaultStructuredBufferViewDesc(clusterProbeGridInfoBuffer, sizeof(ClusterProbeGridInfo)) };

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, clusterProbeGridInfoBuffer->buffer.Get(), &defaultSrvDesc));
			}
			else
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVBufferNullDescription();

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
		}
		break;
		case HASH("ProbePositions"):
		{
			// Should be the same as case above
			const std::string probePositionsResourceName = "ClusterProbeGridInfo";

			ResourceManager& resMan = ResourceManager::Inst();

			if (resMan.FindResource(probePositionsResourceName) == nullptr)
			{
				std::vector<XMFLOAT4> probePositions = Renderer::Inst().GenBakeProbePositions();

				if (probePositions.empty() == false)
				{
					ResourceDesc desc;
					desc.width = probePositions.size() * sizeof(XMFLOAT4);
					desc.height = 1;
					desc.format = DXGI_FORMAT_UNKNOWN;
					desc.dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
					desc.flags = D3D12_RESOURCE_FLAG_NONE;

					FArg::CreateStructuredBuffer args;
					args.context = &ctx.jobContext;
					args.desc = &desc;
					args.data = reinterpret_cast<std::byte*>(probePositions.data());
					args.name = probePositionsResourceName.c_str();

					resMan.CreateStructuredBuffer(args);
				}
			}

			if (Resource* probePositionsBuffer = resMan.FindResource(probePositionsResourceName))
			{
				ViewDescription_t defaultSrvDesc{
					DescriptorHeapUtils::GenerateDefaultStructuredBufferViewDesc(probePositionsBuffer, sizeof(XMFLOAT4)) };

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, probePositionsBuffer->buffer.Get(), &defaultSrvDesc));
			}
			else
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVBufferNullDescription();

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
		}
		break;
		default:
			break;
		}
	}

	/*  Local */

	template<typename pT, typename bT>
	void RegisterLocalPass(unsigned int passName, unsigned int paramName, pT& pass, bT& bindPoint, RegisterLocalPassContext& ctx)
	{
		switch (passName)
		{
		case HASH("BSPClusterClassification"):
		{
		}
		break;
		default:
			break;
		}
	}

	template<typename pT, typename bT>
	void UpdateLocalPass(unsigned int passName, unsigned int paramName, pT& pass, bT& bindPoint, UpdateLocalPassContext& ctx)
	{
		switch (passName)
		{
		case HASH("BSPClusterClassification"):
		{
		}
		break;
		case HASH("SampleIndirect"):
		{
			switch (paramName)
			{
			case HASH("FrameRandSeed"):
			{
				int& frameRandSeed = reinterpret_cast<int&>(bindPoint);
				frameRandSeed = Renderer::Inst().GetCurrentFrameIndex();
			}
			break;
			case HASH("ProbeDataExist"):
			{
				//#DEBUG quick hack. Fix this
				const std::string clusterProbeGridInfoResourceName = "ClusterProbeGridInfo";

				int& dataExist = reinterpret_cast<int&>(bindPoint);
				dataExist = ResourceManager::Inst().FindResource(clusterProbeGridInfoResourceName) == nullptr ? 0 : 1;
			}
			break;
			case HASH("ProbeGridInterval"):
			{
				float& probeGridInterval = reinterpret_cast<float&>(bindPoint);
				probeGridInterval = Settings::CLUSTER_PROBE_GRID_INTERVAL;
			}
			break;
			default:
				break;
			}
		}
		break;
		default:
			break;
		}
	}

	template<typename oT, typename bT>
	void RegisterLocalObject(unsigned int passName, unsigned int paramName, oT& obj, bT& bindPoint, RegisterLocalObjectContext& ctx)
	{
		using objT = std::decay_t<oT>;

		if constexpr (std::is_same_v<objT, DrawCall_UI_t>)
		{
			std::visit([&](auto&& obj)
			{
				using drawCallT = std::decay_t<decltype(obj)>;

				switch (passName)
				{
				case HASH("UI"):
				{
				
				}
				break;
				default:
					break;
				}

			}, obj);

		}
		else if constexpr (std::is_same_v<objT, StaticObject>)
		{
			switch (passName)
			{
			case HASH("Static"):
			{

			}
			break;
			default:
				break;
			}
		}
		else if constexpr (std::is_same_v<objT, DebugObject_t>)
		{
			switch (passName)
			{
			case HASH("Debug"):
			{

			}
			break;
			default:
				break;
			}
		}
		else
		{
			DX_ASSERT(false && "RegisterLocalObject unknown type");
		}
	}

	template<typename oT, typename bT>
	void UpdateLocalObject(unsigned int passName, unsigned int paramName, const oT& obj, bT& bindPoint, UpdateLocalObjectContext& ctx)
	{
		using objT = std::decay_t<oT>;
		
		if constexpr(std::is_same_v<objT, DrawCall_UI_t>)
		{
			// All UI handling is here
			// All UI passes are handled here
			switch (passName)
			{
			case HASH("UI"):
			{
			
			}
			break;
			default:
				break;
			}

		}
		else if constexpr (std::is_same_v<objT, StaticObject>)
		{
			switch (passName)
			{
			case HASH("Static"):
			{

			}
			break;
			default:
				break;
			}
		}
		else if constexpr (std::is_same_v<objT, DebugObject_t>)
		{
			switch (passName)
			{
			case HASH("Debug"):
			{
				switch (paramName)
				{
				case HASH("gCenter"):
				{
					std::visit([&bindPoint](auto&& object) 
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_LightProbe>)
						{
							reinterpret_cast<XMFLOAT4&>(bindPoint) = object.position;
						}
						else
						{
							reinterpret_cast<XMFLOAT4&>(bindPoint) = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
						}
					}, obj);
				}
				break;
				case HASH("gProbeIndex"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_LightProbe>)
						{
							reinterpret_cast<int&>(bindPoint) = object.probeIndex;
						}
						else
						{
							reinterpret_cast<int&>(bindPoint) = Const::INVALID_INDEX;
						}

					}, obj);
				}
				break;
				default:
					break;
				}
			}
			break;
			case HASH("Debug_LightSources"):
			{
				switch (paramName)
				{
				case HASH("gLightSourceType"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_LightSource>)
						{
							reinterpret_cast<int&>(bindPoint) = static_cast<int>(object.type);
						}
						else
						{
							reinterpret_cast<int&>(bindPoint) = Const::INVALID_INDEX;
						}
					}, obj);
				}
				default:
					break;
				}
			}
			break;
			case HASH("Debug_PathSegments"): 
			{
				switch (paramName)
				{
				case HASH("gBounceNum"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_ProbePathSegment>)
						{

							reinterpret_cast<int&>(bindPoint) = static_cast<int>(object.bounce);
						}
						else
						{
							reinterpret_cast<int&>(bindPoint) = -1;
						}
					}, obj);
				}
				case HASH("gRadiance"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_ProbeLightSample>)
						{

							reinterpret_cast<XMFLOAT4&>(bindPoint) = object.radiance;
						}
						else
						{
							reinterpret_cast<XMFLOAT4&>(bindPoint) = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
						}
					}, obj);
				}
				default:
					break;
				}
			}
			break;
			default:
				break;
			}
		}
		else
		{
			DX_ASSERT(false && "UpdateLocalObject unknown type");
		}
	}
}