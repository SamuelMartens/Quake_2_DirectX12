#include "dx_texture.h"

#include "dx_app.h"
#include "dx_resourcemanager.h"

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

	samplerInd = other.samplerInd;

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
