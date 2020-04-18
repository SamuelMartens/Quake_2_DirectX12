#pragma once

#include <d3d12.h>
#include <memory>
#include <string>

#include "d3dx12.h"
#include "dx_common.h"

// Implements exclusive ownership of SRV descriptor
class TextureView
{
public:

	constexpr static int EMPTY_SRV_IND = -1;

	TextureView() = default;
	TextureView(int newSrvInd);

	TextureView(const TextureView&) = delete;
	TextureView& operator=(const TextureView&) = delete;

	TextureView(TextureView&& t);
	TextureView& operator=(TextureView&& t);

	~TextureView();

	int srvIndex = EMPTY_SRV_IND;

};


class Texture
{
public:

	ComPtr<ID3D12Resource> buffer;
	std::shared_ptr<TextureView> texView;

	std::string name;

	int samplerInd = 0;

	int width = 0;
	int height = 0;
	// bits per pixel
	int bpp = 0;

	~Texture();
};