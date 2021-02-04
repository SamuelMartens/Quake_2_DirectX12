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

	width = other.width;
	height = other.height;

	bpp = other.bpp;

	return *this;
}

Texture::~Texture()
{
	// This is a bit lame cause, resource might actually not be deleted, if 
	// some other texture owns it.
	ResourceManager::Inst().RequestResourceDeletion(buffer);
}
