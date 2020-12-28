#pragma once

#include <d3d12.h>
#include <array>

#include "dx_utils.h"
#include "dx_common.h"
#include "dx_jobmultithreading.h"
#include "dx_texture.h"

namespace FArg 
{
	struct UpdateUploadHeapBuff
	{
		ComPtr<ID3D12Resource> buffer;
		int offset = Const::INVALID_OFFSET;
		const void* data = nullptr;
		int byteSize = Const::INVALID_SIZE;
		int alignment = -1;
	};

	struct UpdateDefaultHeapBuff
	{
		ComPtr<ID3D12Resource> buffer;
		int offset = Const::INVALID_OFFSET;
		const void* data = nullptr;
		int byteSize = Const::INVALID_SIZE;
		int alignment = -1;
		JobContext* context = nullptr;
	};
};

class ResourceManager
{
public:

	DEFINE_SINGLETON(ResourceManager);

	/* Buffers */
	ComPtr<ID3D12Resource> CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, JobContext& context);
	ComPtr<ID3D12Resource> CreateUploadHeapBuffer(UINT64 byteSize) const;
	void UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const;
	void UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args);

	/* Resource management */
	void RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete);
	void DeleteRequestedResources();

	/* Textures */
	Texture* FindOrCreateTexture(std::string_view textureName, JobContext& context);
	Texture* FindTexture(std::string_view textureName);
	
	Texture* CreateTextureFromFileDeferred(const char* name, Frame& frame);
	Texture* CreateTextureFromFile(const char* name, JobContext& context);
	Texture* CreateTextureFromDataDeferred(const std::byte* data, int width, int height, int bpp, const char* name, Frame& frame);
	Texture* CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, JobContext& context);
	void CreateDeferredTextures(JobContext& context);

	void GetDrawTextureFullname(const char* name, char* dest, int destSize) const;
	void UpdateTexture(Texture& tex, const std::byte* data, JobContext& context);
	void ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);

private:

	/* Textures */
	Texture* _CreateTextureFromData(const std::byte* data, int width, int height, int bpp, const char* name, JobContext& context);
	Texture* _CreateTextureFromFile(const char* name, JobContext& context);
	void _CreateGpuTexture(const unsigned int* raw, int width, int height, int bpp, JobContext& context, Texture& outTex);


private:

	// If we want to delete resource we can't do this right away cause there is a high chance that this 
	// resource will still be in use, so we just put it here and delete it later 
	LockVector_t<ComPtr<ID3D12Resource>> resourcesToDelete;

	LockUnorderedMap_t<std::string, Texture> textures;

};