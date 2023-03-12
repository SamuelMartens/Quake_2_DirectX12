#include "dx_resource.h"

#include "dx_app.h"
#include "Lib/crc32.h"

Resource::Resource(Resource&& other)
{
	*this = std::move(other);
}

Resource& Resource::operator=(Resource&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	gpuBuffer = other.gpuBuffer;

	if (other.gpuBuffer != nullptr)
	{
		ResourceManager::Inst().RequestResourceDeletion(other.gpuBuffer);
		other.gpuBuffer = nullptr;
	}

	name = std::move(other.name);

	desc = other.desc;

	cpuBuffer = std::move(other.cpuBuffer);

	return *this;
}

Resource::~Resource()
{
	if (gpuBuffer != nullptr)
	{
		// This is a bit lame cause, buffer might actually not be deleted, if 
		// some other resource owns it.
		ResourceManager::Inst().RequestResourceDeletion(gpuBuffer);
	}
}

int Resource::BytesPerPixelFromFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return 4;
	case DXGI_FORMAT_R8_UNORM:
		return 1;
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
		return 4;
	default:
		DX_ASSERT(false && "Unknown format for BPPFromFormat");
		break;
	}

	return 0;
}

int Resource::GetBytesPerPixel(const ResourceDesc& desc)
{
	switch (desc.dimension)
	{
	case D3D12_RESOURCE_DIMENSION_BUFFER:
		return 8;
	case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
		return BytesPerPixelFromFormat(desc.format);
	default:
		DX_ASSERT(false && "Unknown resource dimension");
		break;
	}

	return 0;
}

std::string Resource::GetReadbackResourceNameFromRequest(const ResourceReadBackRequest& request, int frameNumber)
{
	return std::format("{}_{}_{}_READBACK", request.targetResourceName, request.targetPassName, frameNumber);
}

ResourceProxy::ResourceProxy(ID3D12Resource& initResource):
	resource(initResource)
{}

ResourceProxy::ResourceProxy(ID3D12Resource& initResource, D3D12_RESOURCE_STATES initState):
	resource(initResource),
	state(initState)
{}

ResourceProxy::ResourceProxy(ID3D12Resource& initResource, D3D12_RESOURCE_STATES initState, D3D12_RESOURCE_STATES initInterPassState):
	resource(initResource),
	state(initState),
	interPassState(initInterPassState)
{}

void ResourceProxy::TransitionTo(D3D12_RESOURCE_STATES newSate, ID3D12GraphicsCommandList* commandList)
{
	if (state == newSate)
	{
		return;
	}

	CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
		&resource,
		state,
		newSate
	);

	commandList->ResourceBarrier(1, &transition);

	state = newSate;
}

void ResourceProxy::FindAndTranslateTo(const std::string& name, std::vector<ResourceProxy>& proxies, D3D12_RESOURCE_STATES newSate, ID3D12GraphicsCommandList* commandList)
{
	unsigned int hashedName = HASH(name.c_str());

	auto targetProxyIt = std::find_if(proxies.begin(), proxies.end(), [hashedName]
	(const ResourceProxy& proxy)
	{
		return proxy.hashedName == hashedName;
	});

	DX_ASSERT(targetProxyIt != proxies.end() && "FindAndTranslateTo failed. Can't find target proxy");

	targetProxyIt->TransitionTo(newSate, commandList);
}
