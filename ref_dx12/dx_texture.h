#pragma once

#include <d3d12.h>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_utils.h"

class Texture
{
public:

	constexpr static char	RAW_TEXTURE_NAME[] = "__DX_MOVIE_TEXTURE__";
	constexpr static char	FONT_TEXTURE_NAME[] = "conchars";

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


struct TexCreationRequest_FromFile
{
	explicit TexCreationRequest_FromFile(Texture& val) :
		texture(val)
	{}

	Texture& texture;
};

struct TexCreationRequest_FromData
{
	explicit TexCreationRequest_FromData(Texture& val) :
		texture(val)
	{}

	Texture& texture;
	std::vector<std::byte> data;
};

using TextureCreationRequest_t = std::variant<TexCreationRequest_FromData, TexCreationRequest_FromFile>;