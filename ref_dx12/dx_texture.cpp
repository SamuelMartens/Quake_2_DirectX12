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
