#include "dx_texture.h"

#include "dx_app.h"

Texture::Texture(Texture&& other)
{
	*this = std::move(other);
}

Texture& Texture::operator=(Texture&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	buffer = other.buffer;
	other.buffer = nullptr;

	texViewIndex = other.texViewIndex;
	other.texViewIndex = Const::INVALID_INDEX;

	name = std::move(other.name);

	samplerInd = other.samplerInd;

	width = other.width;
	height = other.height;

	bpp = other.bpp;

	return *this;
}

Texture::~Texture()
{
	if (texViewIndex != Const::INVALID_INDEX)
	{
		Renderer::Inst().cbvSrvHeap->Delete(texViewIndex);
	}

	// This is a bit lame cause, resource might actually not be deleted, if 
	// some other texture owns it.
	Renderer::Inst().RequestResourceDeletion(buffer);
}
