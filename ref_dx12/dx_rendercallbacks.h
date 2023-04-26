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
	namespace Utils
	{
		template<typename bufferInstanceType, typename bindPointType, typename contextType>
		void CreateAndBindIfResourceExists_SRV(const char* resourceName, bindPointType& bindPoint, contextType& ctx)
		{
			Resource* resource = ResourceManager::Inst().FindResource(resourceName);
			if (resource == nullptr)
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVBufferNullDescription();
				DO_IF_SAME_DECAYED_TYPE(bindPointType, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
			else
			{
				ViewDescription_t defaultSrvDesc{
					DescriptorHeapUtils::GenerateDefaultStructuredBufferSRVDesc(resource, sizeof(bufferInstanceType)) };

				DO_IF_SAME_DECAYED_TYPE(bindPointType, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, resource->gpuBuffer.Get(), &defaultSrvDesc));
			}
		}

		template<typename bufferInstanceType, typename bindPointType, typename contextType>
		void CreateAndBindIfResourceExists_UAV(const char* resourceName, bindPointType& bindPoint, contextType& ctx)
		{
			Resource* resource = ResourceManager::Inst().FindResource(resourceName);
			if (resource == nullptr)
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetUAVBufferNullDescription();
				DO_IF_SAME_DECAYED_TYPE(bindPointType, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
			else
			{
				ViewDescription_t defaultSrvDesc{
					DescriptorHeapUtils::GenerateDefaultStructuredBufferUAVDesc(resource, sizeof(bufferInstanceType)) };

				DO_IF_SAME_DECAYED_TYPE(bindPointType, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, resource->gpuBuffer.Get(), &defaultSrvDesc));
			}
		}
	}

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
				case HASH("DiffuseMap"):
				{
					Renderer& renderer = Renderer::Inst();

					if constexpr (std::is_same_v< uiDrawCallT, DrawCall_Pic>)
					{
						std::array<char, MAX_QPATH> texFullName;
						ResourceManager::Inst().GetDrawTextureFullname(obj.name.c_str(), texFullName.data(), texFullName.size());

						Resource* tex = ResourceManager::Inst().FindOrCreateResource(texFullName.data(), ctx.jobContext, false);

						ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
						DO_IF_SAME_DECAYED_TYPE(bT, int, 
							renderer.cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->gpuBuffer.Get(), &emtpySrvDesc));

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
			case HASH("DiffuseMap"):
			{
				Resource* tex = ResourceManager::Inst().FindResource(obj.textureKey.c_str());

				ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
				DO_IF_SAME_DECAYED_TYPE( bT, int, 
					Renderer::Inst().cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->gpuBuffer.Get(), &emtpySrvDesc));
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
			case HASH("DiffuseMap"):
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
					renderer.cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->gpuBuffer.Get(), &emtpySrvDesc));
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
				case HASH("WorldViewProj"):
				{
					XMMATRIX mvpMat = XMMatrixTranslation(obj.x, obj.y, 0.0f);

					XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), mvpMat * XMLoadFloat4x4(&ctx.viewProjMat));
				}
				break;
				case HASH("UIObjType"):
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
			
		}
		else if constexpr (std::is_same_v<objT, DebugObject_t>)
		{
			switch (paramName)
			{
			case HASH("DebugObjectType"):
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
					else if constexpr (std::is_same_v<T, DebugObject_FrustumCluster>)
					{
						reinterpret_cast<int&>(bindPoint) = static_cast<int>(DebugObjectType::FrustumClusters);
					}
					else if constexpr (std::is_same_v<T, DebugObject_LightBoundingVolume>)
					{
						reinterpret_cast<int&>(bindPoint) = static_cast<int>(DebugObjectType::LightBoundingVolume);
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

	template<typename ResT, typename bT>
	void RegisterInternalResourceDescriptor(bT& bindPoint, const ResT& rootArg)
	{
		// For internal resource there is no difference if it is global or local,
		// object or pass. No context passed as well because essentially those kind of
		// resources are handled on GPU side.
		Resource* res = ResourceManager::Inst().FindResource(*rootArg.internalBindName);

		DX_ASSERT(res != nullptr && "Can register internal resource. Target texture doesn't exist");

		ViewDescription_t descriptorDesc;
		if constexpr( std::is_same_v<ResT, RootArg::DescTableEntity_Texture>)
		{
			descriptorDesc = ViewDescription_t{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
		} 
		else if constexpr (std::is_same_v<ResT, RootArg::DescTableEntity_UAView>)
		{
			if (res->desc.dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC desc = 
					DescriptorHeapUtils::GenerateDefaultStructuredBufferUAVDesc(res, *rootArg.strideInBytes);

 				descriptorDesc = ViewDescription_t{ std::optional(desc) };
			}
			else
			{
				descriptorDesc = ViewDescription_t{ std::optional<D3D12_UNORDERED_ACCESS_VIEW_DESC>(std::nullopt) };
			} 
		}
		else
		{
			DX_ASSERT(false && "Not implemented");
		}

		DO_IF_SAME_DECAYED_TYPE(bT, int, Renderer::Inst().cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, res->gpuBuffer.Get(), &descriptorDesc));
	}

	template<typename bT>
	void RegisterGlobalPass(unsigned int paramName, bT& bindPoint, RegisterGlobalPassContext& ctx)
	{
		switch (paramName)
		{
		case HASH("FontTex"): 
		{
			std::array<char, MAX_QPATH> texFullName;
			ResourceManager& resMan = ResourceManager::Inst();

			resMan.GetDrawTextureFullname(Resource::FONT_TEXTURE_NAME, texFullName.data(), texFullName.size());

			Resource* tex = resMan.FindResource(texFullName.data());
			if (tex == nullptr)
			{
				FArg::CreateTextureFromFile createTexArgs;
				createTexArgs.name = texFullName.data();
				createTexArgs.context = &ctx.jobContext;
				createTexArgs.saveResourceInCPUMemory = false;

				tex = resMan.CreateTextureFromFile(createTexArgs);
			}

			ViewDescription_t emtpySrvDesc{ std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>(std::nullopt) };
			DO_IF_SAME_DECAYED_TYPE(bT, int, Renderer::Inst().cbvSrvHeapAllocator->AllocateDescriptor(bindPoint, tex->gpuBuffer.Get(), &emtpySrvDesc));
		}
		break;
		case HASH("DiffuseProbes"):
		{
		}
		break;
		case HASH("LightBoundingVolumes"):
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
				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, tex->gpuBuffer.Get(), &emtpySrvDesc));
			}
			else
			{
				ViewDescription_t nullViewDescription = DescriptorHeapUtils::GetSRVTexture2DNullDescription();
				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint, nullptr, &nullViewDescription));
			}
		}
		break;
		case HASH("ViewProj"):
		{
			XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), ctx.jobContext.frame.camera.GetViewProjMatrix());
		}
		break;
		case HASH("View"):
		{
			XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), ctx.jobContext.frame.camera.GenerateViewMatrix());
		}
		break;
		case HASH("CameraYaw"):
		{
			auto[yaw, pitch, roll] = ctx.jobContext.frame.camera.GetBasis();

			reinterpret_cast<XMFLOAT4&>(bindPoint) = yaw;
		}
		break;
		case HASH("CameraPitch"):
		{
			auto[yaw, pitch, roll] = ctx.jobContext.frame.camera.GetBasis();

			reinterpret_cast<XMFLOAT4&>(bindPoint) = pitch;
		}
		break;
		case HASH("CameraRoll"):
		{
			auto[yaw, pitch, roll] = ctx.jobContext.frame.camera.GetBasis();

			reinterpret_cast<XMFLOAT4&>(bindPoint) = roll;
		}
		break;
		case HASH("CameraOrigin"):
		{
			reinterpret_cast<XMFLOAT4&>(bindPoint) = ctx.jobContext.frame.camera.position;
		}
		break;
		case HASH("CameraNear"):
		{
			reinterpret_cast<float&>(bindPoint) = Camera::Z_NEAR;
		}
		break;
		case HASH("CameraFar"):
		{
			reinterpret_cast<float&>(bindPoint) = Camera::Z_FAR;
		}
		break;
		case HASH("DiffuseProbes"):
		{
			Utils::CreateAndBindIfResourceExists_SRV<DiffuseProbe::DiffuseSH_t>(Resource::PROBE_STRUCTURED_BUFFER_NAME, bindPoint, ctx);
		}
		break;
		case HASH("LightBoundingVolumes"):
		{
			Utils::CreateAndBindIfResourceExists_SRV<GPULightBoundingVolume>(Resource::LIGHT_BOUNDING_VOLUME_LIST_NAME, bindPoint, ctx);
		}
		break;
		case HASH("LightsList"):
		{
			Utils::CreateAndBindIfResourceExists_SRV<GPULight>(Resource::LIGHT_LIST_NAME, bindPoint, ctx);
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
					std::vector<::Utils::AABB> clusterAABB(clusterSet.size());

					std::transform(clusterSet.cbegin(), clusterSet.cend(), clusterAABB.begin(),
						[&bspTree](const int clusterIndex)
					{
						return bspTree.GetClusterAABB(clusterIndex);
					});

					ResourceDesc desc;
					desc.width = clusterAABB.size() * sizeof(::Utils::AABB);
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

			Utils::CreateAndBindIfResourceExists_SRV<::Utils::AABB>(clusterAABBResourceName.c_str(), bindPoint, ctx);

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
		case HASH("ClusterProbeGridInfoBuffer"):
		{
			Utils::CreateAndBindIfResourceExists_SRV<ClusterProbeGridInfo>(Resource::CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME, bindPoint, ctx);
		}
		break;
		case HASH("ScreenWidth"):
		{
			int screenWidth = 0;
			int screenHeight = 0;

			Renderer::Inst().GetDrawAreaSize(&screenWidth, &screenHeight);
			
			reinterpret_cast<int&>(bindPoint) = screenWidth;
		}
		break;
		case HASH("ScreenHeight"):
		{
			int screenWidth = 0;
			int screenHeight = 0;

			Renderer::Inst().GetDrawAreaSize(&screenWidth, &screenHeight);

			reinterpret_cast<int&>(bindPoint) = screenHeight;
		}
		break;
		case HASH("ClusterListSize"):
		{
			reinterpret_cast<int&>(bindPoint) = ctx.jobContext.frame.camera.GetFrustumClustersNum();
		}
		break;
		case HASH("InvertedViewProj"):
		{
			XMMATRIX sseInvViewProjMatrix = ctx.jobContext.frame.camera.GetViewProjMatrix();
			XMVECTOR sseDeterminant = XMVectorZero();

			sseInvViewProjMatrix = XMMatrixInverse(&sseDeterminant, sseInvViewProjMatrix);

			DX_ASSERT(XMVectorGetX(sseDeterminant) != 0.0f && "Matrix determinant is zero, inverse is wrong");
			XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint),
				 sseInvViewProjMatrix);
		}
		break;
		case HASH("InvertedProj"):
		{
			XMMATRIX sseInvProjMatrix = ctx.jobContext.frame.camera.GenerateProjectionMatrix();
			XMVECTOR sseDeterminant = XMVectorZero();

			sseInvProjMatrix = XMMatrixInverse(&sseDeterminant, sseInvProjMatrix);

			DX_ASSERT(XMVectorGetX(sseDeterminant) != 0.0f && "Matrix determinant is zero, inverse is wrong");
			XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint),
				sseInvProjMatrix);
		}
		break;
		case HASH("TileWidth"):
		{
			reinterpret_cast<int&>(bindPoint) = Camera::FRUSTUM_TILE_WIDTH;
		}
		break;
		case HASH("TileHeight"):
		{ 
			reinterpret_cast<int&>(bindPoint) = Camera::FRUSTUM_TILE_HEIGHT;
		}
		break;
		case HASH("NumFrustumSlices"):
		{
			reinterpret_cast<int&>(bindPoint) = Camera::FRUSTUM_CLUSTER_SLICES;
		}
		break;
		case HASH("LightListSize"):
		{
			int lightListSize = 0;

			// Make sure light list resource exists
			const bool isLightListResourceExists = ResourceManager::Inst().FindResource(Resource::LIGHT_LIST_NAME) != nullptr;

			if (isLightListResourceExists == true)
			{
				const int staticAreaLights = Renderer::Inst().GetStaticAreaLights().size();
				const int staticPointLights = Renderer::Inst().GetStaticPointLights().size();
				
				lightListSize = staticAreaLights + staticPointLights;
			}

			reinterpret_cast<int&>(bindPoint) = lightListSize;
		}
		break;
		case HASH("DepthBuffer"):
		{
			ViewDescription_t genericDesc = std::optional(D3D12_SHADER_RESOURCE_VIEW_DESC());

			D3D12_SHADER_RESOURCE_VIEW_DESC& viewDesc = std::get<std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>>(genericDesc).value();
			viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
			viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			viewDesc.Shader4ComponentMapping =
				D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
					D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
					D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
					D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
					D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
				);
			viewDesc.Texture2D.MostDetailedMip = 0;
			viewDesc.Texture2D.MipLevels = 1;
			viewDesc.Texture2D.PlaneSlice = 0;
			viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

			DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint,
				ctx.jobContext.frame.depthStencilBuffer.Get(), &genericDesc));
		}
		case HASH("LightSourcePickerEnabled"):
		{
			reinterpret_cast<int&>(bindPoint) = ctx.jobContext.frame.debugEnableLightSourcePicker;
		}
		break;
		case HASH("MousePosX"):
		{
			reinterpret_cast<int&>(bindPoint) = ctx.jobContext.frame.mouseInput.position.x;
		}
		break;
		case HASH("MousePosY"):
		{
			reinterpret_cast<int&>(bindPoint) = ctx.jobContext.frame.mouseInput.position.y;
		}
		break;
		case HASH("LeftMouseButtonDown"):
		{
			reinterpret_cast<int&>(bindPoint) = ctx.jobContext.frame.mouseInput.leftButtonDown;
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
				frameRandSeed = Renderer::Inst().GetCurrentFrameCounter();
			}
			break;
			case HASH("ProbeDataExist"):
			{
				int& dataExist = reinterpret_cast<int&>(bindPoint);
				dataExist = ResourceManager::Inst().FindResource(Resource::PROBE_STRUCTURED_BUFFER_NAME) == nullptr ? 0 : 1;
			}
			break;
			case HASH("ProbeGridInterval"):
			{
				float& probeGridInterval = reinterpret_cast<float&>(bindPoint);
				probeGridInterval = Settings::CLUSTER_PROBE_GRID_INTERVAL;
			}
			break;
			case HASH("DepthBuffer"):
			{
				ViewDescription_t genericDesc = std::optional(D3D12_SHADER_RESOURCE_VIEW_DESC());

				D3D12_SHADER_RESOURCE_VIEW_DESC& viewDesc = std::get<std::optional<D3D12_SHADER_RESOURCE_VIEW_DESC>>(genericDesc).value();
				viewDesc.Format = DXGI_FORMAT_R32_FLOAT;
				viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				viewDesc.Shader4ComponentMapping = 
					D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
						D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
						D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
						D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
						D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0
					);
				viewDesc.Texture2D.MostDetailedMip = 0;
				viewDesc.Texture2D.MipLevels = 1;
				viewDesc.Texture2D.PlaneSlice = 0;
				viewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

				DO_IF_SAME_DECAYED_TYPE(bT, int, ctx.jobContext.frame.streamingCbvSrvAllocator->AllocateDescriptor(bindPoint,
					ctx.jobContext.frame.depthStencilBuffer.Get(), &genericDesc));
			}
			break;
			default:
				break;
			}
		}
		break;
		case HASH("Debug_Line"):
		{
			switch (paramName)
			{
			case HASH("FrustumClusterInvertedView"):
			{
				reinterpret_cast<XMFLOAT4X4&>(bindPoint) = ctx.jobContext.frame.debugFrustumClusterInverseViewMat;
			}
			break;
			default:
				break;
			}
		}
		break;
		case HASH("Debug_LightSourcePicker"):
		{
			switch (paramName)
			{
			case HASH("PickedLights"):
			{
				Utils::CreateAndBindIfResourceExists_UAV<uint32_t>(Resource::DEBUG_PICKED_LIGHTS_LIST_NAME, bindPoint, ctx);
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
			case HASH("PickedLights"):
			{
				Utils::CreateAndBindIfResourceExists_SRV<uint32_t>(Resource::DEBUG_PICKED_LIGHTS_LIST_NAME, bindPoint, ctx);
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
			case HASH("Debug_Triangle"):
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
			case HASH("Debug_Triangle"):
			{
				switch (paramName)
				{
				case HASH("ProbeCenter"):
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
				case HASH("ProbeIndex"):
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
			case HASH("Debug_TriangleWireframe"):
			{
				switch (paramName)
				{
				case HASH("BoundingVolumeIndex"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_LightBoundingVolume>)
						{
							reinterpret_cast<int&>(bindPoint) = static_cast<int>(object.sourceIndex);
						}
						else
						{
							reinterpret_cast<int&>(bindPoint) = -1;
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
				case HASH("LightSourceType"):
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
				break;
				case HASH("LightSourceIndex"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_LightSource>)
						{
							const int globalSourceIndex = Renderer::Inst().GetLightIndexInStaticLightList(object.sourceIndex, object.type);
							reinterpret_cast<int&>(bindPoint) = static_cast<int>(globalSourceIndex);
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
			case HASH("Debug_Line"): 
			{
				switch (paramName)
				{
				case HASH("BounceNum"):
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
				break;
				case HASH("Radiance"):
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
				break;
				case HASH("FrustumClusterIndex"):
				{
					std::visit([&bindPoint](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_FrustumCluster>)
						{
							reinterpret_cast<int&>(bindPoint) = object.index;
						}
						else
						{
							reinterpret_cast<int&>(bindPoint) = Const::INVALID_INDEX;
						}
					}, obj);
				}
				break;
				case HASH("IsActiveFrustumCluster"):
				{
					std::visit([&bindPoint, &ctx](auto&& object)
					{
						using T = std::decay_t<decltype(object)>;

						if constexpr (std::is_same_v<T, DebugObject_FrustumCluster>)
						{
							reinterpret_cast<int&>(bindPoint) = static_cast<int>(object.isActive);
						}
						else
						{
							reinterpret_cast<int&>(bindPoint) = static_cast<int>(false);
						}
					}, obj);
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
		else if constexpr (std::is_same_v<objT, entity_t>)
		{
			switch (passName)
			{
			case HASH("Dynamic"):
			{
				switch (paramName)
				{
				case HASH("WorldViewProj"):
				{
					XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint),
						DynamicObjectModel::GenerateModelMat(obj) * ctx.jobContext.frame.camera.GetViewProjMatrix());
				}
				break;
				case HASH("World"):
				{
					XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint),
						DynamicObjectModel::GenerateModelMat(obj));
				}
				break;
				case HASH("AnimMove"):
				{
					const DynamicObjectModel& model = Renderer::Inst().GetDynamicModels().at(obj.model);
					auto [animMove, frontLerp, backLerp] = model.GenerateAnimInterpolationData(obj);

					reinterpret_cast<XMFLOAT4&>(bindPoint) = animMove;
				}
				break;
				case HASH("FrontLerp"):
				{
					const DynamicObjectModel& model = Renderer::Inst().GetDynamicModels().at(obj.model);
					auto [animMove, frontLerp, backLerp] = model.GenerateAnimInterpolationData(obj);

					reinterpret_cast<XMFLOAT4&>(bindPoint) = frontLerp;
				}
				break;
				case HASH("BackLerp"):
				{
					const DynamicObjectModel& model = Renderer::Inst().GetDynamicModels().at(obj.model);
					auto [animMove, frontLerp, backLerp] = model.GenerateAnimInterpolationData(obj);

					reinterpret_cast<XMFLOAT4&>(bindPoint) = backLerp;
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
		else
		{
			DX_ASSERT(false && "UpdateLocalObject unknown type");
		}
	}
}