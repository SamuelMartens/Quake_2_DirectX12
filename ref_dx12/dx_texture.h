#pragma once

#include <d3d12.h>
#include <memory>
#include <string>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_utils.h"

class Texture
{
public:

	Texture() = default;

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	Texture(Texture&& other);
	Texture& operator=(Texture&& other);

	~Texture();
	
	ComPtr<ID3D12Resource> buffer;
	int texViewIndex = Const::INVALID_INDEX;

	std::string name;

	int samplerInd = 0;

	int width = 0;
	int height = 0;
	// bits per pixel
	int bpp = 0;

};