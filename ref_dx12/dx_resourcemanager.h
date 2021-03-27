#pragma once

#include <d3d12.h>
#include <array>

#include "dx_utils.h"
#include "dx_common.h"
#include "dx_threadingutils.h"
#include "dx_texture.h"

namespace FArg 
{
	struct UpdateUploadHeapBuff
	{
		ID3D12Resource* buffer = nullptr;
		int offset = Const::INVALID_OFFSET;
		const void* data = nullptr;
		int byteSize = Const::INVALID_SIZE;
		int alignment = -1;
	};

	struct UpdateDefaultHeapBuff
	{
		ID3D12Resource* buffer = nullptr;
		int offset = Const::INVALID_OFFSET;
		const void* data = nullptr;
		int byteSize = Const::INVALID_SIZE;
		int alignment = -1;
		GPUJobContext* context = nullptr;
	};

	struct ZeroUploadHeapBuff
	{
		ID3D12Resource* buffer = nullptr;
		int offset = Const::INVALID_OFFSET;
		int byteSize = Const::INVALID_SIZE;
	};

	struct VerifyZeroUploadHeapBuff
	{
		ID3D12Resource* buffer = nullptr;
		int offset = Const::INVALID_OFFSET;
		int byteSize = Const::INVALID_SIZE;
	};

	struct CreateTextureFromData
	{
		const std::byte* data = nullptr;
		const TextureDesc* desc = nullptr;
		const char* name = nullptr;
		GPUJobContext* context = nullptr;
		const XMFLOAT4* clearValue = nullptr;
	};

	struct CreateTextureFromDataDeferred
	{
		const std::byte* data = nullptr;
		const TextureDesc* desc = nullptr;
		const char* name = nullptr;
		Frame* frame = nullptr;
	};

	struct _CreateGpuTexture
	{
		const unsigned int* raw = nullptr; 
		const TextureDesc* desc = nullptr;
		GPUJobContext* context = nullptr; 
		Texture* outTex = nullptr;
		const XMFLOAT4* clearValue = nullptr;
	};
};

class ResourceManager
{
public:

	DEFINE_SINGLETON(ResourceManager);
	
	/* Buffers */
	ComPtr<ID3D12Resource> CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, GPUJobContext& context);
	ComPtr<ID3D12Resource> CreateUploadHeapBuffer(UINT64 byteSize) const;
	void UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const;
	void UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args);

	void ZeroMemoryUploadHeapBuff(FArg::ZeroUploadHeapBuff& args);
	void VerifyZeroUploadHeapBuff(FArg::VerifyZeroUploadHeapBuff& args) const;

	/* Resource management */
	void RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete);
	void DeleteRequestedResources();

	/* Textures */
	Texture* FindOrCreateTexture(std::string_view textureName, GPUJobContext& context);
	Texture* FindTexture(std::string_view textureName);
	
	Texture* CreateTextureFromFileDeferred(const char* name, Frame& frame);
	Texture* CreateTextureFromFile(const char* name, GPUJobContext& context);
	Texture* CreateTextureFromDataDeferred(FArg::CreateTextureFromDataDeferred& args);
	Texture* CreateTextureFromData(FArg::CreateTextureFromData& args);
	void CreateDeferredTextures(GPUJobContext& context);

	void GetDrawTextureFullname(const char* name, char* dest, int destSize) const;
	void UpdateTexture(Texture& tex, const std::byte* data, GPUJobContext& context);
	void ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);
	void DeleteTexture(const char* name);

private:

	/* Textures */
	Texture* _CreateTextureFromData(FArg::CreateTextureFromData& args);
	Texture* _CreateTextureFromFile(const char* name, GPUJobContext& context);
	void _CreateGpuTexture(FArg::_CreateGpuTexture& args);


private:

	// If we want to delete resource we can't do this right away cause there is a high chance that this 
	// resource will still be in use, so we just put it here and delete it later 
	LockVector_t<ComPtr<ID3D12Resource>> resourcesToDelete;

	LockUnorderedMap_t<std::string, Texture> textures;

};