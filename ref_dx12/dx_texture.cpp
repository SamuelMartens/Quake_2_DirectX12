#include "dx_texture.h"

#include "dx_app.h"

TextureView::TextureView(TextureView&& t)
{
	if (&t == this)
	{
		return;
	}

	srvIndex = t.srvIndex;
	t.srvIndex = EMPTY_SRV_IND;
}

TextureView::TextureView(int newSrvInd) :
	srvIndex(newSrvInd)
{}

TextureView::~TextureView()
{
	if (srvIndex != EMPTY_SRV_IND)
	{
		Renderer::Inst().FreeSrvSlot(srvIndex);
	}
}

TextureView& TextureView::operator=(TextureView&& t)
{
	if (&t != this)
	{
		srvIndex = t.srvIndex;
		t.srvIndex = EMPTY_SRV_IND;
	}

	return *this;
}

Texture::~Texture()
{
	// This is a bit lame cause, resource might actually not be deleted, if 
	// some other texture owns it.
	Renderer::Inst().DeleteResources(buffer);
}
