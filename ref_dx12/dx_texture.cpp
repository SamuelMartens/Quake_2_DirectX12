#include "dx_texture.h"

#include "dx_app.h"
#include "dx_resourcemanager.h"
#include "Lib/crc32.h"

Texture::Texture(Texture&& other)
{
	*this = std::move(other);
}

Texture& Texture::operator=(Texture&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	buffer = other.buffer;

	ResourceManager::Inst().RequestResourceDeletion(other.buffer);
	other.buffer = nullptr;

	name = std::move(other.name);

	desc = other.desc;

	return *this;
}

Texture::~Texture()
{
	// This is a bit lame cause, resource might actually not be deleted, if 
	// some other texture owns it.
	ResourceManager::Inst().RequestResourceDeletion(buffer);
}

int Texture::BPPFromFormat(DXGI_FORMAT format)
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

ResourceProxy::ResourceProxy(ID3D12Resource& initTexture):
	resource(initTexture)
{}

ResourceProxy::ResourceProxy(ID3D12Resource& initTexture, D3D12_RESOURCE_STATES initState):
	resource(initTexture),
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
