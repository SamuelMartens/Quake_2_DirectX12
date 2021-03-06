#include "dx_resourcemanager.h"

#include "dx_infrastructure.h"
#include "dx_diagnostics.h"
#include "dx_app.h"
#include "dx_threadingutils.h"


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
	assert(args.buffer != nullptr &&
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
	assert(args.buffer != nullptr &&
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
	assert(args.buffer != nullptr && 
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

	assert(memcmp(mappedMemory + args.offset, v.data(), args.byteSize) == 0 && "VerifyZeroUploadHeapBuff failed.");

	args.buffer->Unmap(0, &mappedRange);
}

void ResourceManager::RequestResourceDeletion(ComPtr<ID3D12Resource> resourceToDelete)
{
	std::scoped_lock<std::mutex> lock(resourcesToDelete.mutex);

	resourcesToDelete.obj.push_back(resourceToDelete);
}

void ResourceManager::DeleteRequestedResources()
{
	std::scoped_lock<std::mutex> lock(resourcesToDelete.mutex);

	resourcesToDelete.obj.clear();
}

Texture* ResourceManager::FindOrCreateTexture(std::string_view textureName, GPUJobContext& context)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	Texture* texture = nullptr;
	auto texIt = textures.obj.find(textureName.data());

	if (texIt != textures.obj.end())
	{
		texture = &texIt->second;
	}
	else
	{
		texture = _CreateTextureFromFile(textureName.data(), context);
	}

	return texture;
}

Texture* ResourceManager::FindTexture(std::string_view textureName)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	auto texIt = textures.obj.find(textureName.data());

	return texIt == textures.obj.end() ? nullptr : &texIt->second;
}

Texture* ResourceManager::CreateTextureFromFileDeferred(const char* name, Frame& frame)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	Texture tex;
	tex.name = name;

	Texture* result = &textures.obj.insert_or_assign(tex.name, std::move(tex)).first->second;

	TexCreationRequest_FromFile texRequest(*result);
	frame.texCreationRequests.push_back(std::move(texRequest));

	return result;
}

Texture* ResourceManager::CreateTextureFromFile(const char* name, GPUJobContext& context)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	return _CreateTextureFromFile(name, context);
}

Texture* ResourceManager::CreateTextureFromDataDeferred(FArg::CreateTextureFromDataDeferred& args)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	Texture tex;
	tex.name = args.name;
	tex.desc = *args.desc;

	Texture* result = &textures.obj.insert_or_assign(tex.name, std::move(tex)).first->second;

	TexCreationRequest_FromData texRequest(*result);
	const int texSize = tex.desc.width * tex.desc.height;
	
	texRequest.data.resize(texSize);
	memcpy(texRequest.data.data(), args.data, texSize);

	args.frame->texCreationRequests.push_back(std::move(texRequest));

	return result;
}

Texture* ResourceManager::CreateTextureFromData(FArg::CreateTextureFromData& args)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	return _CreateTextureFromData(args);
}

void ResourceManager::CreateDeferredTextures(GPUJobContext& context)
{
	CommandListRAIIGuard_t commandListGuard(*context.commandList);

	std::scoped_lock<std::mutex> lock(textures.mutex);

	for (const TextureCreationRequest_t& tr : context.frame.texCreationRequests)
	{
		std::visit([&context, this](auto&& texRequest)
		{
			using T = std::decay_t<decltype(texRequest)>;

			if constexpr (std::is_same_v<T, TexCreationRequest_FromFile>)
			{
				std::string name = texRequest.texture.name;
				_CreateTextureFromFile(name.c_str(), context);
			}
			else if constexpr (std::is_same_v<T, TexCreationRequest_FromData>)
			{
				// Get tex actual color
				const int textureSize = texRequest.texture.desc.width * texRequest.texture.desc.height;
				std::vector<unsigned int> texture(textureSize, 0);

				const auto& rawPalette = Renderer::Inst().GetRawPalette();

				for (int i = 0; i < textureSize; ++i)
				{
					texture[i] = rawPalette[std::to_integer<int>(texRequest.data[i])];
				}

				std::string name = texRequest.texture.name;

				FArg::CreateTextureFromData createTexArgs;
				createTexArgs.data = reinterpret_cast<std::byte*>(texture.data());
				createTexArgs.desc = &texRequest.texture.desc;
				createTexArgs.name = name.c_str();
				createTexArgs.context = &context;

				_CreateTextureFromData(createTexArgs);
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
		assert(destSize >= strlen(name) + 1);
		strcpy(dest, name + 1);
	}
}

void ResourceManager::UpdateTexture(Texture& tex, const std::byte* data, GPUJobContext& context)
{
	Logs::Logf(Logs::Category::Textures, "Update Texture with name %s", tex.name.c_str());

	CommandList& commandList = *context.commandList;

	// Count alignment and go for what we need
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex.buffer.Get(), 0, 1);

	ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);
	Diagnostics::SetResourceNameWithAutoId(textureUploadBuffer.Get(), "TextureUploadBuffer_UpdateTexture");

	DO_IN_LOCK(context.frame.uploadResources, push_back(textureUploadBuffer));

	D3D12_SUBRESOURCE_DATA textureData = {};
	textureData.pData = data;
	// Divide by 8 cause bpp is bits per pixel, not bytes
	textureData.RowPitch = tex.desc.width * Texture::BPPFromFormat(tex.desc.format) / 8;
	// Not SlicePitch but texture size in our case
	textureData.SlicePitch = textureData.RowPitch * tex.desc.height;

	CD3DX12_RESOURCE_BARRIER copyDestTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		tex.buffer.Get(),
		Texture::DEFAULT_STATE,
		D3D12_RESOURCE_STATE_COPY_DEST
	);

	commandList.GetGPUList()->ResourceBarrier(1, &copyDestTransition);

	UpdateSubresources(commandList.GetGPUList(), tex.buffer.Get(), textureUploadBuffer.Get(), 0, 0, 1, &textureData);

	CD3DX12_RESOURCE_BARRIER pixelResourceTransition = CD3DX12_RESOURCE_BARRIER::Transition(
		tex.buffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		Texture::DEFAULT_STATE);

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

void ResourceManager::DeleteTexture(const char* name)
{
	std::scoped_lock<std::mutex> lock(textures.mutex);

	Logs::Logf(Logs::Category::Textures, "Delete texture %s", name);

	auto texIt = textures.obj.find(name);

	assert(texIt != textures.obj.end() && "Trying to delete texture that doesn't exist");
	
	textures.obj.erase(texIt);
}

Texture* ResourceManager::_CreateTextureFromData(FArg::CreateTextureFromData& args)
{
	Logs::Logf(Logs::Category::Textures, "Create texture %s", args.name);

	Texture tex;

	FArg::_CreateGpuTexture createTexArgs;
	createTexArgs.raw = reinterpret_cast<const unsigned int*>(args.data);
	createTexArgs.desc = args.desc;
	createTexArgs.context = args.context;
	createTexArgs.outTex = &tex;
	createTexArgs.clearValue = args.clearValue;

	_CreateGpuTexture(createTexArgs);

	tex.desc = *args.desc;
	tex.name = args.name;

	Diagnostics::SetResourceName(tex.buffer.Get(), tex.name);

	return &textures.obj.insert_or_assign(tex.name, std::move(tex)).first->second;
}

Texture* ResourceManager::_CreateTextureFromFile(const char* name, GPUJobContext& context)
{
	if (name == nullptr)
		return nullptr;

	std::array<char, MAX_QPATH> nonConstName;
	// Some old functions access only non const pointer, that's just work around
	char* nonConstNamePtr = nonConstName.data();
	strcpy(nonConstNamePtr, name);


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
		assert(false && "Invalid texture file extension");
		return nullptr;
	}

	if (image == nullptr)
	{
		assert(false && "Failed to create texture from file");
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
	const TextureDesc desc = { width, height, format };

	FArg::CreateTextureFromData createTexArgs;
	createTexArgs.data = reinterpret_cast<std::byte*>(image32);
	createTexArgs.desc = &desc;
	createTexArgs.name = name;
	createTexArgs.context = &context;

	Texture* createdTex = _CreateTextureFromData(createTexArgs);

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

void ResourceManager::_CreateGpuTexture(FArg::_CreateGpuTexture& args)
{
	D3D12_RESOURCE_DESC textureDesc = {};
	textureDesc.MipLevels = 1;
	textureDesc.Format = args.desc->format;
	textureDesc.Width = args.desc->width;
	textureDesc.Height = args.desc->height;
	textureDesc.Flags = args.desc->flags;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	const D3D12_RESOURCE_STATES initState = args.raw != nullptr ?
		D3D12_RESOURCE_STATE_COPY_DEST : 
		Texture::DEFAULT_STATE;

	D3D12_CLEAR_VALUE clearValue;
	ZeroMemory(&clearValue, sizeof(clearValue));
	
	if (args.clearValue != nullptr)
	{
		clearValue.Format = textureDesc.Format;
		
		clearValue.Color[0] = args.clearValue->x;
		clearValue.Color[1] = args.clearValue->y;
		clearValue.Color[2] = args.clearValue->z;
		clearValue.Color[3] = args.clearValue->w;
	}

	// Create destination texture
	ThrowIfFailed(Infr::Inst().GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		initState,
		args.clearValue == nullptr ? nullptr : &clearValue,
		IID_PPV_ARGS(&args.outTex->buffer)));

	if (args.raw != nullptr)
	{
		assert(args.context != nullptr && "If texture is initialized on creation GPU Context is required");

		CommandList& commandList = *args.context->commandList;

		// Count alignment and go for what we need
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(args.outTex->buffer.Get(), 0, 1);

		ComPtr<ID3D12Resource> textureUploadBuffer = CreateUploadHeapBuffer(uploadBufferSize);
		Diagnostics::SetResourceNameWithAutoId(textureUploadBuffer.Get(), "TextureUploadBuffer_CreateTexture");

		DO_IN_LOCK(args.context->frame.uploadResources, push_back(textureUploadBuffer));

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = args.raw;
		// Divide by 8 cause bpp is bits per pixel, not bytes
		textureData.RowPitch = args.desc->width * Texture::BPPFromFormat(args.desc->format) / 8;
		// Not SlicePitch but texture size in our case
		textureData.SlicePitch = textureData.RowPitch * args.desc->height;

		UpdateSubresources(commandList.GetGPUList(), args.outTex->buffer.Get(), textureUploadBuffer.Get(), 0, 0, 1, &textureData);
		
		CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
			args.outTex->buffer.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			Texture::DEFAULT_STATE);
		commandList.GetGPUList()->ResourceBarrier(1, &transition);
	}

}
