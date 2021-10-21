#include "dx_resource.h"

#include "dx_app.h"
#include "dx_resourcemanager.h"
#include "Lib/crc32.h"

Resource::Resource(Resource&& other)
{
	*this = std::move(other);
}

Resource& Resource::operator=(Resource&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	buffer = other.buffer;

	ResourceManager::Inst().RequestResourceDeletion(other.buffer);
	other.buffer = nullptr;

	name = std::move(other.name);

	desc = other.desc;

	return *this;
}

Resource::~Resource()
{
	// This is a bit lame cause, buffer might actually not be deleted, if 
	// some other resource owns it.
	ResourceManager::Inst().RequestResourceDeletion(buffer);
}

int Resource::BPPFromFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return 32;
	case DXGI_FORMAT_R8_UNORM:
		return 8;
	default:
		assert(false && "Unknown format for BPPFromFormat");
		break;
	}

	return 0;
}

ResourceProxy::ResourceProxy(ID3D12Resource& initResource):
	resource(initResource)
{}

ResourceProxy::ResourceProxy(ID3D12Resource& initResource, D3D12_RESOURCE_STATES initState):
	resource(initResource),
	state(initState)
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

	assert(targetProxyIt != proxies.end() && "FindAndTranslateTo failed. Can't find target proxy");

	targetProxyIt->TransitionTo(newSate, commandList);
}
