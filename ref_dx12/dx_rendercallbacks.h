#pragma once

#include "Lib/crc32.h"
#include "dx_renderstage.h"
#include "dx_descriptorheap.h"
#include "dx_texture.h"
#include "dx_app.h"
#include "dx_resourcemanager.h"

extern "C"
{
	#include "../client/ref.h"
};


namespace RenderCallbacks
{
	using ConstBuffBind_t = std::byte;
	//#DEBUG int is a bit too generic. Need to think about something better
	using ResourceViewBind_t = int;

	struct PerPassUpdateContext
	{
	};

	struct PerObjectUpdateContext
	{
		XMFLOAT4X4 viewProjMat;
		Context& jobCtx;
	};

	struct PerObjectRegisterContext
	{
		Context& jobCtx;
	};

	template<typename sT, typename bT>
	void PerPassUpdateCallback(unsigned int name, sT& stage, bT& bindPoint, PerPassUpdateContext& ctx)
	{
		using bindT = std::decay_t<bT>;
		using stageT = std::decay_t<sT>;

		if constexpr(std::is_same_v<stageT, RenderStage_UI>)
		{
			// All UI stages are handled here
			switch (name)
			{
			case HASH("UI"):
			{
				if constexpr (std::is_same_v<bindT, ConstBuffBind_t>)
				{
					// All static pass const buff data
				}
				else if constexpr(std::is_same_v<stageT, ResourceViewBind_t>)
				{
					// All static pass view data
				}
			}
				break;
			default:
				break;
			}
		}
	}

	template<typename oT, typename bT>
	void PerObjectUpdateCallback(unsigned int passName, unsigned int paramName, const oT& obj, bT* bindPoint, PerObjectUpdateContext& ctx)
	{
		using bindT = std::decay_t<bT>;
		using objT = std::decay_t<oT>;

		if constexpr(std::is_same_v<objT, DrawCall_Char> ||
			std::is_same_v<objT, DrawCall_Pic> ||
			std::is_same_v<objT, DrawCall_StretchRaw>)
		{
			// All UI handling is here
			// All UI stages are handled here
			switch (passName)
			{
			case HASH("UI"):
			{
				if constexpr (std::is_same_v < bindT, ConstBuffBind_t>)
				{
					// All static pass const buff data
					switch (paramName)
					{
					case HASH("gWorldViewProj"):
					{
						XMMATRIX mvpMat = XMMatrixTranslation(obj.x, obj.y, 0.0f);
						
						XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(bindPoint), mvpMat * XMLoadFloat4x4(&ctx.viewProjMat));
					}
						break;
					default:
						break;
					}
				}
				else if constexpr (std::is_same_v<bindT, ResourceViewBind_t>)
				{
					// All static pass view data
					switch (paramName)
					{
					default:
						break;
					}
				}
			}
			break;
			default:
				break;
			}

		}
	}

	template<typename oT, typename bT>
	void PerObjectRegisterCallback(unsigned int passName, unsigned int paramName, oT& obj, bT* bindPoint, PerObjectRegisterContext& ctx) 
	{
		using bindT = std::decay_t<bT>;
		using objT = std::decay_t<oT>;

		if constexpr (std::is_same_v<objT, DrawCall_UI_t>)
		{
			// All UI handling is here
			// All UI stages are handled here
			switch (passName)
			{
			case HASH("UI"):
			{
				if constexpr (std::is_same_v < bindT, ConstBuffBind_t>)
				{
					// All static pass const buff data
					switch (paramName)
					{
					default:
						break;
					}
				}
				else if constexpr (std::is_same_v<bindT, ResourceViewBind_t>)
				{
					// All static pass view data
					switch (paramName)
					{
					case HASH("gDiffuseMap"):
					{
						std::visit([&ctx, bindPoint](auto&& drawCall) 
						{
							using drawCallT = std::decay_t<decltype(drawCall)>;

							if constexpr (std::is_same_v<drawCallT ,DrawCall_Pic>)
							{
								Renderer& renderer = Renderer::Inst();

								std::array<char, MAX_QPATH> texFullName;
								ResourceManager::Inst().GetDrawTextureFullname(drawCall.name.c_str(), texFullName.data(), texFullName.size());

								Texture* tex = ResourceManager::Inst().FindOrCreateTexture(texFullName.data(), ctx.jobCtx);
								//#DEBUG this might be nullptr and texture will be the same as base texture?
								DescriptorHeap::Desc_t srvDescription = D3D12_SHADER_RESOURCE_VIEW_DESC{};
								D3D12_SHADER_RESOURCE_VIEW_DESC& srvDescriptionRef = std::get<D3D12_SHADER_RESOURCE_VIEW_DESC>(srvDescription);
								srvDescriptionRef.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
								//#DEBUG format should be part of texture
								srvDescriptionRef.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
								srvDescriptionRef.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
								srvDescriptionRef.Texture2D.MostDetailedMip = 0;
								srvDescriptionRef.Texture2D.MipLevels = 1;
								srvDescriptionRef.Texture2D.ResourceMinLODClamp = 0.0f;

								renderer.cbvSrvHeap->AllocateDescriptor(*bindPoint ,tex->buffer.Get(), &srvDescription);
								
							}

							if constexpr (std::is_same_v<drawCallT, DrawCall_Char>)
							{
								std::array<char, MAX_QPATH> texFullName;
								ResourceManager& resMan = ResourceManager::Inst();

								resMan.GetDrawTextureFullname(Texture::FONT_TEXTURE_NAME, texFullName.data(), texFullName.size());


								Texture* tex = resMan.FindTexture(texFullName.data());
								if (tex == nullptr)
								{
									tex = resMan.CreateTextureFromFile(texFullName.data(), ctx.jobCtx);
								}

								DescriptorHeap::Desc_t srvDescription = D3D12_SHADER_RESOURCE_VIEW_DESC{};
								D3D12_SHADER_RESOURCE_VIEW_DESC& srvDescriptionRef = std::get<D3D12_SHADER_RESOURCE_VIEW_DESC>(srvDescription);
								srvDescriptionRef.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
								srvDescriptionRef.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
								srvDescriptionRef.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
								srvDescriptionRef.Texture2D.MostDetailedMip = 0;
								srvDescriptionRef.Texture2D.MipLevels = 1;
								srvDescriptionRef.Texture2D.ResourceMinLODClamp = 0.0f;

								Renderer::Inst().cbvSrvHeap->AllocateDescriptor(*bindPoint , tex->buffer.Get(), &srvDescription);

							}

							if constexpr (std::is_same_v<drawCallT, DrawCall_StretchRaw>)
							{
								Texture* tex = ResourceManager::Inst().FindTexture(Texture::RAW_TEXTURE_NAME);
								assert(tex != nullptr && "Draw_RawPic texture doesn't exist");

								// If there is no data, then texture is requested to be created for this frame. So no need to update
								if (drawCall.data.empty() == false)
								{
									const int textureSize = drawCall.textureWidth * drawCall.textureHeight;

									CommandList& commandList = ctx.jobCtx.commandList;

									std::vector<unsigned int> texture(textureSize, 0);
									const std::array<unsigned int, 256>&  rawPalette = Renderer::Inst().GetRawPalette();

									for (int i = 0; i < textureSize; ++i)
									{
										texture[i] =  rawPalette[std::to_integer<int>(drawCall.data[i])];
									}

									ResourceManager::Inst().UpdateTexture(*tex, reinterpret_cast<std::byte*>(texture.data()), ctx.jobCtx);
								}

								//#DEBUG MAKE PROPER DELETION OF HANDLES AND VIEW
								DescriptorHeap::Desc_t srvDescription = D3D12_SHADER_RESOURCE_VIEW_DESC{};
								D3D12_SHADER_RESOURCE_VIEW_DESC& srvDescriptionRef = std::get<D3D12_SHADER_RESOURCE_VIEW_DESC>(srvDescription);
								srvDescriptionRef.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
								srvDescriptionRef.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
								srvDescriptionRef.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
								srvDescriptionRef.Texture2D.MostDetailedMip = 0;
								srvDescriptionRef.Texture2D.MipLevels = 1;
								srvDescriptionRef.Texture2D.ResourceMinLODClamp = 0.0f;

								Renderer::Inst().cbvSrvHeap->AllocateDescriptor(*bindPoint , tex->buffer.Get(), &srvDescription);
							}

						}, obj);
					}
						break;
					default:
						break;
					}
				}
			}
			break;
			default:
				break;
			}

		}
		else
		{
			static_assert(false, "Unknown type");
		}
	}
}