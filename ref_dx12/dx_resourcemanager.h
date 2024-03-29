#pragma once

#include <d3d12.h>
#include <array>

#include "dx_utils.h"
#include "dx_common.h"
#include "dx_threadingutils.h"
#include "dx_resource.h"

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

	struct CreateResource
	{
		const std::byte* data = nullptr;
		const ResourceDesc* desc = nullptr;
		const char* name = nullptr;
		GPUJobContext* context = nullptr;
		const XMFLOAT4* clearValue = nullptr;
		bool saveResourceInCPUMemory = false;
	};

	struct CreateResourceFromDataDeferred
	{
		const std::byte* data = nullptr;
		const ResourceDesc* desc = nullptr;
		const char* name = nullptr;
		Frame* frame = nullptr;
		const XMFLOAT4* clearValue = nullptr;
		bool saveResourceInCPUMemory = false;
	};

	struct _CreateGpuResource
	{
		const unsigned int* raw = nullptr; 
		const ResourceDesc* desc = nullptr;
		GPUJobContext* context = nullptr; 
		const XMFLOAT4* clearValue = nullptr;
	};

	struct _CreateTextureFromFile
	{
		const char* name = nullptr;
		GPUJobContext* context = nullptr;
		bool saveResourceInCPUMemory = false;
	};
	
	struct CreateStructuredBuffer
	{
		const ResourceDesc* desc = nullptr;
		const char* name = nullptr;
		GPUJobContext* context = nullptr;
		const std::byte* data = nullptr;
	};

	struct CreateTextureFromFile
	{
		const char* name = nullptr;
		GPUJobContext* context = nullptr;
		bool saveResourceInCPUMemory = false;
	};

	struct CreateTextureFromFileDeferred
	{
		const char* name = nullptr;
		Frame* frame = nullptr;
		bool saveResourceInCPUMemory = false;
	};
};

class ResourceManager
{
public:

	DEFINE_SINGLETON(ResourceManager);
	
	/* Buffer low level */
	ComPtr<ID3D12Resource> CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, GPUJobContext& context);
	ComPtr<ID3D12Resource> CreateUploadHeapBuffer(UINT64 byteSize) const;
	ComPtr<ID3D12Resource> CreateReadBackHeapBuffer(UINT64 byteSize) const;

	/* Utils */
	void UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const;
	void UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args);
	void ZeroMemoryUploadHeapBuff(FArg::ZeroUploadHeapBuff& args);
	void VerifyZeroUploadHeapBuff(FArg::VerifyZeroUploadHeapBuff& args) const;

	/* Structured buffers */
	Resource* CreateStructuredBuffer(FArg::CreateStructuredBuffer& args);

	/* Textures */
	Resource* CreateTextureFromFileDeferred(FArg::CreateTextureFromFileDeferred& args);
	Resource* CreateTextureFromFile(FArg::CreateTextureFromFile& args);
	Resource* CreateResourceFromDataDeferred(FArg::CreateResourceFromDataDeferred& args);
	Resource* CreateTextureFromData(FArg::CreateResource& args);
	void CreateDeferredResource(GPUJobContext& context);

	void GetDrawTextureFullname(const char* name, char* dest, int destSize) const;
	void UpdateResource(Resource& res, const std::byte* data, GPUJobContext& context);
	void ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight);

	/* Resource management */
	Resource* RegisterD3DResource(ComPtr<ID3D12Resource> d3dResource, const ResourceDesc* desc, const std::string& resourceName);
	Resource* FindOrCreateResource(std::string_view resourceName, GPUJobContext& context, bool saveResourceInCPUMemory);
	Resource* FindResource(std::string_view resourceName);

	void RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete);
	void DeleteRequestedResources();
	void DeleteResource(const char* name);

private:

	/* Textures */
	Resource* _CreateTextureFromFile(FArg::_CreateTextureFromFile& args);
	
	/* Generic resource */
	Resource* _CreateResource(FArg::CreateResource& args);
	ComPtr<ID3D12Resource> _CreateGpuResource(FArg::_CreateGpuResource& args);


private:

	// If we want to delete resource we can't do this right away cause there is a high chance that this 
	// resource will still be in use, so we just put it here and delete it later 
	LockVector_t<ComPtr<ID3D12Resource>> resourcesToDelete;

	LockUnorderedMap_t<std::string, Resource> resources;

};