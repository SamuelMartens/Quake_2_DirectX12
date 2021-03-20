#pragma once

#include <d3d12.h>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_utils.h"

struct TextureDesc
{
	int width = 0;
	int height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
};

//#DEBUG delete this
class ResourceManager;

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
	
	// bits per pixel from format
	static int BPPFromFormat(DXGI_FORMAT format);
	
public:

	ComPtr<ID3D12Resource> buffer;

	std::string name;
	
	TextureDesc desc;
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

/*
	I need to manage state of internal resources. I can't keep 
	this data inside Texture class itself, because multiple jobs might work
	on this resource in the same time, so what I am going to do is just have this
	proxy object that every frame will use to access it's internal texture
	NOTE: this is only intended for internal resources. 
*/
struct ResourceProxy
{
	// When a job is started it expects resource to be in this state.
	// When a job is finished it leaves resource in this state.
	const static D3D12_RESOURCE_STATES INTER_JOB_STATE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	explicit ResourceProxy(ID3D12Resource& initTexture);
	ResourceProxy(ID3D12Resource& initTexture, D3D12_RESOURCE_STATES initState);

	void TransitionTo(D3D12_RESOURCE_STATES newSate, ID3D12GraphicsCommandList* commandList);
	
	void static FindAndTranslateTo(
		const std::string& name,
		std::vector<ResourceProxy>& proxies,
		D3D12_RESOURCE_STATES newSate,
		ID3D12GraphicsCommandList* commandList);
	
	//#DEBUG this is bad. Only resource can own this. Not proxy
	ID3D12Resource& resource;
	unsigned int hashedName = Const::INVALID_HASHED_NAME;

private:

	D3D12_RESOURCE_STATES state = INTER_JOB_STATE;
};

using TextureCreationRequest_t = std::variant<TexCreationRequest_FromData, TexCreationRequest_FromFile>;