#pragma once

#include <functional>

#include "Lib/crc32.h"
#include "dx_pass.h"
#include "dx_descriptorheap.h"
#include "dx_texture.h"
#include "dx_app.h"
#include "dx_resourcemanager.h"
#include "dx_framegraph.h"

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
					if constexpr (std::is_same_v< uiDrawCallT, DrawCall_Pic>)
					{
						Renderer& renderer = Renderer::Inst();

						std::array<char, MAX_QPATH> texFullName;
						ResourceManager::Inst().GetDrawTextureFullname(obj.name.c_str(), texFullName.data(), texFullName.size());

						Texture* tex = ResourceManager::Inst().FindOrCreateTexture(texFullName.data(), ctx.jobContext);

						renderer.cbvSrvHeap->AllocateDescriptor(bindPoint, tex->buffer.Get(), nullptr);

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
				Texture* tex = ResourceManager::Inst().FindTexture(obj.textureKey.c_str());

				Renderer::Inst().cbvSrvHeap->AllocateDescriptor(bindPoint, tex->buffer.Get(), nullptr);
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
				assert(obj.skin == nullptr && "Custom skin. I am not prepared for this");

				Renderer& renderer = Renderer::Inst();

				const DynamicObjectModel& model = renderer.GetDynamicModels().at(obj.model);

				Texture* tex = nullptr;

				if (obj.skinnum >= MAX_MD2SKINS)
				{
					tex = ResourceManager::Inst().FindTexture(model.textures[0]);
				}
				else
				{
					tex = ResourceManager::Inst().FindTexture(model.textures[obj.skinnum]);

					if (tex == nullptr)
					{
						tex = ResourceManager::Inst().FindTexture(model.textures[0]);
					}
				}

				assert(tex != nullptr && "Not texture found for dynamic object rendering. Implement fall back");

				renderer.cbvSrvHeap->AllocateDescriptor(bindPoint, tex->buffer.Get(), nullptr);
			}
			break;
			default:
				break;
			}
		}
		else
		{
			assert(false && "RegisterGlobalObject unknown object type");
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
		else
		{
			assert(false && "UpdateGlobalObject unknown object type");
		}

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

			resMan.GetDrawTextureFullname(Texture::FONT_TEXTURE_NAME, texFullName.data(), texFullName.size());

			Texture* tex = resMan.FindTexture(texFullName.data());
			if (tex == nullptr)
			{
				tex = resMan.CreateTextureFromFile(texFullName.data(), ctx.jobContext);
			}

			Renderer::Inst().cbvSrvHeap->AllocateDescriptor(reinterpret_cast<int&>(bindPoint), tex->buffer.Get(), nullptr);
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
			Texture* tex = ResourceManager::Inst().FindTexture(Texture::RAW_TEXTURE_NAME);
			assert(tex != nullptr && "Draw_RawPic texture doesn't exist");

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

					CommandList& commandList = ctx.jobContext.commandList;

					std::vector<unsigned int> texture(textureSize, 0);
					const std::array<unsigned int, 256>&  rawPalette = Renderer::Inst().GetRawPalette();

					for (int i = 0; i < textureSize; ++i)
					{
						texture[i] = rawPalette[std::to_integer<int>(movieDrawCall.data[i])];
					}

					ResourceManager::Inst().UpdateTexture(*tex, reinterpret_cast<std::byte*>(texture.data()), ctx.jobContext);
				}

				Renderer::Inst().cbvSrvHeap->AllocateDescriptor(reinterpret_cast<int&>(bindPoint), tex->buffer.Get(), nullptr);
			}
		}
		break;
		case HASH("gViewProj"):
		{
			XMStoreFloat4x4(&reinterpret_cast<XMFLOAT4X4&>(bindPoint), ctx.jobContext.frame.camera.GetViewProjMatrix());
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

	}

	template<typename pT, typename bT>
	void UpdateLocalPass(unsigned int passName, unsigned int paramName, pT& pass, bT& bindPoint, UpdateLocalPassContext& ctx)
	{

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
		else
		{
			assert(false && "RegisterLocalObject unknown type");
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
		else
		{
			assert(false && "UpdateLocalObject unknown type");
		}
	}
}