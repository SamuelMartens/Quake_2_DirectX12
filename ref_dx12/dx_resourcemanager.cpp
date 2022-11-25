#include "dx_resourcemanager.h"

#include "dx_app.h"


ComPtr<ID3D12Resource> ResourceManager::CreateDefaultHeapBuffer(const void* data, UINT64 byteSize, GPUJobContext& context)
{
	// Create actual buffer 
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = byteSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.SampleDesc.Quality = 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ComPtr<ID3D12Resource> buffer;

	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&buffer)
	));

	ID3D12GraphicsCommandList* commandList = context.commandList->GetGPUList();

	if (data != nullptr)
	{
		// Create upload buffer
		ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(byteSize);
		Diagnostics::SetResourceNameWithAutoId(uploadBuffer.Get(), "UploadBuffer_CreateDefaultHeap");

		DO_IN_LOCK(context.frame.uploadResources, push_back(uploadBuffer));

		// Describe upload resource data 
		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data;
		subResourceData.RowPitch = byteSize;
		subResourceData.SlicePitch = subResourceData.RowPitch;

		UpdateSubresources(commandList, buffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	}

	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
		buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ
	);

	commandList->ResourceBarrier(1, &transition);

	return buffer;
}

ComPtr<ID3D12Resource> ResourceManager::CreateUploadHeapBuffer(UINT64 byteSize) const
{
	ComPtr<ID3D12Resource> uploadHeapBuffer;

	// Create actual buffer 
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Alignment = 0;
	bufferDesc.Width = byteSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.SampleDesc.Quality = 0;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadHeapBuffer)
	));

	return uploadHeapBuffer;
}

void ResourceManager::UpdateUploadHeapBuff(FArg::UpdateUploadHeapBuff& args) const
{
	DX_ASSERT(args.buffer != nullptr &&
		args.alignment != -1 &&
		args.byteSize != Const::INVALID_SIZE &&
		args.data != nullptr &&
		args.offset != Const::INVALID_OFFSET && "Uninitialized arguments in update upload buff");

	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;

	BYTE* mappedMemory = nullptr;
	// This parameter indicates the range that CPU might read. If begin and end are equal, we promise
	// that CPU will never try to read from this memory
	D3D12_RANGE mappedRange = { 0, 0 };

	ThrowIfFailed(args.buffer->Map(0, &mappedRange, reinterpret_cast<void**>(&mappedMemory)));

	memcpy(mappedMemory + args.offset, args.data, dataSize);

	D3D12_RANGE unmappedRange = { static_cast<SIZE_T>(args.offset),  static_cast<SIZE_T>(args.offset) + dataSize };
	args.buffer->Unmap(0, &unmappedRange);
}

void ResourceManager::UpdateDefaultHeapBuff(FArg::UpdateDefaultHeapBuff& args)
{
	DX_ASSERT(args.buffer != nullptr &&
		args.alignment != -1 &&
		args.byteSize != Const::INVALID_SIZE &&
		args.data != nullptr &&
		args.offset != Const::INVALID_OFFSET &&
		args.context != nullptr &&
		"Uninitialized arguments in update default buff");

	const unsigned int dataSize = args.alignment != 0 ? Utils::Align(args.byteSize, args.alignment) : args.byteSize;

	Frame& frame = args.context->frame;
	CommandList& commandList = *args.context->commandList;

	// Create upload buffer
	ComPtr<ID3D12Resource> uploadBuffer = CreateUploadHeapBuffer(args.byteSize);
	Diagnostics::SetResourceNameWithAutoId(uploadBuffer.Get(), "UploadBuffer_UpdateDefaultHeap");

	DO_IN_LOCK(frame.uploadResources, push_back(uploadBuffer));

	FArg::UpdateUploadHeapBuff uploadHeapBuffArgs;
	uploadHeapBuffArgs.alignment = 0;
	uploadHeapBuffArgs.buffer = uploadBuffer.Get();
	uploadHeapBuffArgs.byteSize = args.byteSize;
	uploadHeapBuffArgs.data = args.data;
	uploadHeapBuffArgs.offset = 0;
	UpdateUploadHeapBuff(uploadHeapBuffArgs);

	CD3DX12_RESOURCE_BARRIER copyDestTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		args.buffer,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_COPY_DEST
	);

	commandList.GetGPUList()->ResourceBarrier(1, &copyDestTransition);

	// Last argument is intentionally args.byteSize, cause that's how much data we pass to this function
	// we don't want to read out of range
	commandList.GetGPUList()->CopyBufferRegion(args.buffer, args.offset, uploadBuffer.Get(), 0, args.byteSize);

	CD3DX12_RESOURCE_BARRIER readTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		args.buffer,
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ
	);

	commandList.GetGPUList()->ResourceBarrier(1, &readTransition);
}

void ResourceManager::ZeroMemoryUploadHeapBuff(FArg::ZeroUploadHeapBuff& args)
{
	DX_ASSERT(args.buffer != nullptr && 
		args.byteSize != Const::INVALID_SIZE &&
		args.offset != Const::INVALID_OFFSET && "Uninitialized arguments in zero upload buff");

	BYTE* mappedMemory = nullptr;
	// This parameter indicates the range that CPU might read. If begin and end are equal, we promise
	// that CPU will never try to read from this memory
	D3D12_RANGE mappedRange = { 0, 0 };

	ThrowIfFailed(args.buffer->Map(0, &mappedRange, reinterpret_cast<void**>(&mappedMemory)));

	memset(mappedMemory + args.offset, 0, args.byteSize);

	D3D12_RANGE unmappedRange = { static_cast<SIZE_T>(args.offset),  static_cast<SIZE_T>(args.offset) + args.byteSize };
	args.buffer->Unmap(0, &unmappedRange);
}

void ResourceManager::VerifyZeroUploadHeapBuff(FArg::VerifyZeroUploadHeapBuff& args) const
{
	BYTE* mappedMemory = nullptr;
	// This parameter indicates the range that CPU might read. If begin and end are equal, we promise
	// that CPU will never try to read from this memory
	D3D12_RANGE mappedRange = { static_cast<SIZE_T>(args.offset), static_cast<SIZE_T>(args.offset) + args.byteSize };

	std::vector<std::byte> v(args.byteSize, static_cast<std::byte>(0));

	ThrowIfFailed(args.buffer->Map(0, &mappedRange, reinterpret_cast<void**>(&mappedMemory)));

	DX_ASSERT(memcmp(mappedMemory + args.offset, v.data(), args.byteSize) == 0 && "VerifyZeroUploadHeapBuff failed.");

	args.buffer->Unmap(0, &mappedRange);
}

Resource* ResourceManager::CreateStructuredBuffer(FArg::CreateStructuredBuffer& args)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	DX_ASSERT(args.desc->dimension == D3D12_RESOURCE_DIMENSION_BUFFER && "Invalid buffer dimension during resource creation");
	DX_ASSERT(args.desc->format == DXGI_FORMAT_UNKNOWN && "Invalid structured buffer format");

	FArg::CreateResource resCreationArgs;
	resCreationArgs.desc = args.desc;
	resCreationArgs.name = args.name;
	resCreationArgs.context = args.context;
	resCreationArgs.data = args.data;

	return _CreateResource(resCreationArgs);
}

void ResourceManager::RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete)
{
	DX_ASSERT(resourceToDelete != nullptr && "Can't request deletion of empty resource");

	std::scoped_lock<std::mutex> lock(resourcesToDelete.mutex);

	resourcesToDelete.obj.push_back(resourceToDelete);
}

void ResourceManager::DeleteRequestedResources()
{
	// For now I have no mechanism to be sure that resources that needs to be deleted
	// are not used in some frames. So what I do is I force flush all frames and the then delete.
	// Ugly, but it works
	ASSERT_MAIN_THREAD;

	{
		std::scoped_lock<std::mutex> lock(resourcesToDelete.mutex);

		if (resourcesToDelete.obj.size() < Settings::GPU_RESOURCE_DELETION_THRESHOLD)
		{
			return;
		}
	}

	Renderer::Inst().FlushAllFrames();

	{
		std::scoped_lock<std::mutex> lock(resourcesToDelete.mutex);
		resourcesToDelete.obj.clear();
	}
}

Resource* ResourceManager::FindOrCreateResource(std::string_view resourceName, GPUJobContext& context, bool saveResourceInCPUMemory)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	Resource* texture = nullptr;
	auto texIt = resources.obj.find(resourceName.data());

	if (texIt != resources.obj.end())
	{
		texture = &texIt->second;
	}
	else
	{
		FArg::_CreateTextureFromFile args;
		args.name = resourceName.data();
		args.context = &context;
		args.saveResourceInCPUMemory = saveResourceInCPUMemory;

		texture = _CreateTextureFromFile(args);
	}

	return texture;
}

Resource* ResourceManager::FindResource(std::string_view resourceName)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	auto texIt = resources.obj.find(resourceName.data());

	return texIt == resources.obj.end() ? nullptr : &texIt->second;
}

Resource* ResourceManager::CreateTextureFromFileDeferred(FArg::CreateTextureFromFileDeferred& args)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	Resource tex;
	tex.name = args.name;

	Resource* result = &resources.obj.insert_or_assign(tex.name, std::move(tex)).first->second;

	TexCreationRequest_FromFile texRequest(*result);
	texRequest.saveResourceInCPUMemory = args.saveResourceInCPUMemory;

	args.frame->texCreationRequests.push_back(std::move(texRequest));

	return result;
}

Resource* ResourceManager::CreateTextureFromFile(FArg::CreateTextureFromFile& args)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	FArg::_CreateTextureFromFile _args;
	_args.name = args.name;
	_args.context = args.context;
	_args.saveResourceInCPUMemory = args.saveResourceInCPUMemory;

	return _CreateTextureFromFile(_args);
}

Resource* ResourceManager::CreateTextureFromDataDeferred(FArg::CreateTextureFromDataDeferred& args)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	Resource tex;
	tex.name = args.name;
	tex.desc = *args.desc;

	Resource* result = &resources.obj.insert_or_assign(tex.name, std::move(tex)).first->second;

	TexCreationRequest_FromData texRequest(*result);
	texRequest.saveResourceInCPUMemory = args.saveResourceInCPUMemory;
	
	if (args.data != nullptr)
	{
		// In bytes
		const int texSize = tex.desc.width * tex.desc.height * Resource::GetBytesPerPixel(tex.desc);

		texRequest.data.resize(texSize);
		memcpy(texRequest.data.data(), args.data, texSize);
	}

	if (args.clearValue != nullptr)
	{
		texRequest.clearValue = *args.clearValue;
	}

	args.frame->texCreationRequests.push_back(std::move(texRequest));

	return result;
}

Resource* ResourceManager::CreateTextureFromData(FArg::CreateResource& args)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	DX_ASSERT(args.desc->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && "Invalid texture dimension during resource creation");

	return _CreateResource(args);
}

void ResourceManager::CreateDeferredTextures(GPUJobContext& context)
{
	CommandListRAIIGuard_t commandListGuard(*context.commandList);

	std::scoped_lock<std::mutex> lock(resources.mutex);

	for (const TextureCreationRequest_t& tr : context.frame.texCreationRequests)
	{
		std::visit([&context, this](auto&& texRequest)
		{
			using T = std::decay_t<decltype(texRequest)>;

			if constexpr (std::is_same_v<T, TexCreationRequest_FromFile>)
			{
				FArg::_CreateTextureFromFile createTextureArgs;
				createTextureArgs.name = texRequest.texture.name.c_str();
				createTextureArgs.context = &context;
				createTextureArgs.saveResourceInCPUMemory = texRequest.saveResourceInCPUMemory;

				_CreateTextureFromFile(createTextureArgs);
			}
			else if constexpr (std::is_same_v<T, TexCreationRequest_FromData>)
			{
				std::string name = texRequest.texture.name;

				FArg::CreateResource createTexArgs;
				createTexArgs.data = texRequest.data.empty() == true ? nullptr : texRequest.data.data();
				createTexArgs.desc = &texRequest.texture.desc;
				createTexArgs.name = name.c_str();
				createTexArgs.context = &context;
				createTexArgs.clearValue = texRequest.clearValue.has_value() ? &texRequest.clearValue.value() : nullptr;
				createTexArgs.saveResourceInCPUMemory = texRequest.saveResourceInCPUMemory;

				DX_ASSERT(createTexArgs.desc->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && "Invalid texture dimension during resource creation");

				_CreateResource(createTexArgs);
			}
			else
			{
				static_assert(false, "Invalid class in Deferred Tex creation");
			}
		}
		, tr);
	}
}

void ResourceManager::GetDrawTextureFullname(const char* name, char* dest, int destSize) const
{
	if (name[0] != '/' && name[0] != '\\')
	{
		Utils::Sprintf(dest, destSize, "pics/%s.pcx", name);
	}
	else
	{
		DX_ASSERT(destSize >= strlen(name) + 1);
		strcpy(dest, name + 1);
	}
}

void ResourceManager::UpdateResource(Resource& res, const std::byte* data, GPUJobContext& context)
{
	Logs::Logf(Logs::Category::Resource, "Update Resource with name %s", res.name.c_str());

	DX_ASSERT((res.desc.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			res.desc.dimension == D3D12_RESOURCE_DIMENSION_BUFFER) &&
		"UpdateResource received invalid resource type");

	CommandList& commandList = *context.commandList;

	// Count alignment and go for what we need
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(res.gpuBuffer.Get(), 0, 1);

	ComPtr<ID3D12Resource> resourceUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);
	Diagnostics::SetResourceNameWithAutoId(resourceUploadBuffer.Get(), "ResourceUploadBuffer_UpdateResource");

	DO_IN_LOCK(context.frame.uploadResources, push_back(resourceUploadBuffer));

	D3D12_SUBRESOURCE_DATA resourceData = {};
	resourceData.pData = data;
	resourceData.RowPitch = res.desc.width * Resource::GetBytesPerPixel(res.desc);
	// Not SlicePitch but texture size in our case
	resourceData.SlicePitch = resourceData.RowPitch * res.desc.height;

	CD3DX12_RESOURCE_BARRIER copyDestTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		res.gpuBuffer.Get(),
		Resource::DEFAULT_STATE,
		D3D12_RESOURCE_STATE_COPY_DEST
	);

	commandList.GetGPUList()->ResourceBarrier(1, &copyDestTransition);

	UpdateSubresources(commandList.GetGPUList(), res.gpuBuffer.Get(), resourceUploadBuffer.Get(), 0, 0, 1, &resourceData);

	CD3DX12_RESOURCE_BARRIER pixelResourceTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		res.gpuBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		Resource::DEFAULT_STATE);

	commandList.GetGPUList()->ResourceBarrier(1, &pixelResourceTransition);
}

void ResourceManager::ResampleTexture(const unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight)
{
	// Copied from GL_ResampleTexture
	// Honestly, I don't know exactly what it does, I mean I understand that
	// it most likely upscales image, but how?
	int		i, j;
	const unsigned	*inrow, *inrow2;
	unsigned	frac, fracstep;
	unsigned	p1[1024], p2[1024];
	byte		*pix1, *pix2, *pix3, *pix4;

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;
	for (i = 0; i < outwidth; i++)
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracstep;
	}
	frac = 3 * (fracstep >> 2);
	for (i = 0; i < outwidth; i++)
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++, out += outwidth)
	{
		inrow = in + inwidth * (int)((i + 0.25)*inheight / outheight);
		inrow2 = in + inwidth * (int)((i + 0.75)*inheight / outheight);
		frac = fracstep >> 1;
		for (j = 0; j < outwidth; j++)
		{
			pix1 = (byte *)inrow + p1[j];
			pix2 = (byte *)inrow + p2[j];
			pix3 = (byte *)inrow2 + p1[j];
			pix4 = (byte *)inrow2 + p2[j];
			((byte *)(out + j))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
			((byte *)(out + j))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
			((byte *)(out + j))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
			((byte *)(out + j))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
		}
	}
}

void ResourceManager::DeleteResource(const char* name)
{
	std::scoped_lock<std::mutex> lock(resources.mutex);

	Logs::Logf(Logs::Category::Resource, "Delete resource %s", name);

	auto resIt = resources.obj.find(name);

	DX_ASSERT(resIt != resources.obj.end() && "Trying to delete resource that doesn't exist");
	// This will eventually result in call to RequestResourceDeletion	
	resources.obj.erase(resIt);
}

Resource* ResourceManager::_CreateResource(FArg::CreateResource& args)
{
	Logs::Logf(Logs::Category::Resource, "Create resource %s", args.name);

	DX_ASSERT(args.desc->dimension != D3D12_RESOURCE_DIMENSION_UNKNOWN && "Invalid texture dimension during resource creation");

	Resource res;

	FArg::_CreateGpuResource createTexArgs;
	createTexArgs.raw = reinterpret_cast<const unsigned int*>(args.data);
	createTexArgs.desc = args.desc;
	createTexArgs.context = args.context;
	createTexArgs.clearValue = args.clearValue;

	res.gpuBuffer = _CreateGpuResource(createTexArgs);

	res.desc = *args.desc;
	res.name = args.name;

	// Create and keep CPU resource in buffer, if we want to
	if (args.saveResourceInCPUMemory == true)
	{
		DX_ASSERT(args.data != nullptr && "Can't save resource in CPU memory if no resource data is provided");

		const int resourceSizeInBytes = args.desc->width * args.desc->height * Resource::GetBytesPerPixel(*args.desc);
		
		res.cpuBuffer = std::vector<std::byte>(resourceSizeInBytes);
		memcpy(res.cpuBuffer->data(), args.data, resourceSizeInBytes);
	}

	Diagnostics::SetResourceName(res.gpuBuffer.Get(), res.name);

	return &resources.obj.insert_or_assign(res.name, std::move(res)).first->second;
}


Resource* ResourceManager::_CreateTextureFromFile(FArg::_CreateTextureFromFile& args)
{
	if (args.name == nullptr)
		return nullptr;

	std::array<char, MAX_QPATH> nonConstName;
	// Some old functions access only non const pointer, that's just work around
	char* nonConstNamePtr = nonConstName.data();
	strcpy(nonConstNamePtr, args.name);


	constexpr int fileExtensionLength = 4;
	const char* texFileExtension = nonConstNamePtr + strlen(nonConstNamePtr) - fileExtensionLength;

	std::byte* image = nullptr;
	std::byte* palette = nullptr;
	int width = 0;
	int height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

	if (strcmp(texFileExtension, ".pcx") == 0)
	{
		format = DXGI_FORMAT_R8_UNORM;
		Utils::LoadPCX(nonConstNamePtr, &image, &palette, &width, &height);
	}
	else if (strcmp(texFileExtension, ".wal") == 0)
	{
		format = DXGI_FORMAT_R8_UNORM;
		Utils::LoadWal(nonConstNamePtr, &image, &width, &height);
	}
	else if (strcmp(texFileExtension, ".tga") == 0)
	{
		format = DXGI_FORMAT_R8G8B8A8_UNORM;
		Utils::LoadTGA(nonConstNamePtr, &image, &width, &height);
	}
	else
	{
		DX_ASSERT(false && "Invalid texture file extension");
		return nullptr;
	}

	if (image == nullptr)
	{
		DX_ASSERT(false && "Failed to create texture from file");
		return nullptr;
	}

	unsigned int* image32 = nullptr;
	constexpr int maxImageSize = 512 * 256;
	// This would be great to have this on heap, but it's too big and
	// cause stack overflow immediately on entrance in function
	static unsigned int* fixedImage = new unsigned int[maxImageSize];
#ifdef _DEBUG
	memset(fixedImage, 0, maxImageSize * sizeof(unsigned int));
#endif

	//#TODO I ignore texture type (which is basically represents is this texture sky or
	// or something else. It's kind of important here, as we need to handle pictures
	// differently sometimes depending on type. I will need to take care of it later.
	if (format == DXGI_FORMAT_R8_UNORM)
	{
		Renderer::Inst().ImageBpp8To32(image, width, height, fixedImage);
		format = DXGI_FORMAT_R8G8B8A8_UNORM;

		image32 = fixedImage;
	}
	else
	{
		image32 = reinterpret_cast<unsigned int*>(image);
	}

	// I might need this later
	//int scaledWidth = 0;
	//int scaledHeight = 0;

	//FindImageScaledSizes(width, height, scaledWidth, scaledHeight);

	//std::vector<unsigned int> resampledImage(maxImageSize, 0);

	//if (scaledWidth != width || scaledHeight != height)
	//{
	//	ResampleTexture(image32, width, height, resampledImage.data(), scaledHeight, scaledWidth);
	//	image32 = resampledImage.data();
	//}
	ResourceDesc desc;
	desc.width = width;
	desc.height = height;
	desc.format = format;
	desc.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	FArg::CreateResource createTexArgs;
	createTexArgs.data = reinterpret_cast<std::byte*>(image32);
	createTexArgs.desc = &desc;
	createTexArgs.name = args.name;
	createTexArgs.context = args.context;
	createTexArgs.saveResourceInCPUMemory = args.saveResourceInCPUMemory;

	Resource* createdTex = _CreateResource(createTexArgs);

	createdTex->desc.reflectivity = AreaLight::CalculateReflectivity(*createdTex,
		reinterpret_cast<std::byte*>(image32));
	
	if (image != nullptr)
	{
		free(image);
	}

	if (palette != nullptr)
	{
		free(palette);
	}

	return createdTex;
}

ComPtr<ID3D12Resource> ResourceManager::_CreateGpuResource(FArg::_CreateGpuResource& args)
{
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = args.desc->format;
	resourceDesc.Width = args.desc->width;
	resourceDesc.Height = args.desc->height;
	resourceDesc.Flags = args.desc->flags;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Dimension = args.desc->dimension;
	resourceDesc.Layout = resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? 
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;

	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	const D3D12_RESOURCE_STATES initState = args.raw != nullptr ?
		D3D12_RESOURCE_STATE_COPY_DEST : 
		Resource::DEFAULT_STATE;

	D3D12_CLEAR_VALUE clearValue;
	ZeroMemory(&clearValue, sizeof(clearValue));
	
	if (args.clearValue != nullptr)
	{
		clearValue.Format = resourceDesc.Format;
		
		clearValue.Color[0] = args.clearValue->x;
		clearValue.Color[1] = args.clearValue->y;
		clearValue.Color[2] = args.clearValue->z;
		clearValue.Color[3] = args.clearValue->w;
	}

	ComPtr<ID3D12Resource> resource;

	// Create destination texture
	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		initState,
		args.clearValue == nullptr ? nullptr : &clearValue,
		IID_PPV_ARGS(resource.GetAddressOf())));

	if (args.raw != nullptr)
	{
		DX_ASSERT(args.context != nullptr && "If texture is initialized on creation GPU Context is required");

		CommandList& commandList = *args.context->commandList;

		// Count alignment and go for what we need
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(resource.Get(), 0, 1);

		ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);
		Diagnostics::SetResourceNameWithAutoId(textureUploadBuffer.Get(), "TextureUploadBuffer_CreateTexture");

		DO_IN_LOCK(args.context->frame.uploadResources, push_back(textureUploadBuffer));

		D3D12_SUBRESOURCE_DATA resourceData = {};
		resourceData.pData = args.raw;
		
		resourceData.RowPitch = args.desc->width * Resource::GetBytesPerPixel(*args.desc);
		
		// Not SlicePitch but texture size in our case
		resourceData.SlicePitch = resourceData.RowPitch * args.desc->height;

		UpdateSubresources(commandList.GetGPUList(), resource.Get(), textureUploadBuffer.Get(), 0, 0, 1, &resourceData);
		
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
			resource.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			Resource::DEFAULT_STATE);
		commandList.GetGPUList()->ResourceBarrier(1, &transition);
	}

	return resource;

}
