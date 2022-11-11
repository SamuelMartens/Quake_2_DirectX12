#pragma once

#include <d3d12.h>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_utils.h"

struct ResourceDesc
{

	int width = 0;
	int height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

	// This is awkward to have this data in resource decs. The reason being that from
	// game's perspective each texture (which mode if resources are) can have this values.
	// Perhaps I should move to lighting texture sometime?
	// Flux of the surface = iradiance * reflectivity 
	// Iradiance is just intensity, which describes how much we want to scale reflectivity
	// Reflectivity is normalized vector and describes how much of each component of RGB
	// is emitted by this surface
	int iradiance = 0;
	XMFLOAT4 reflectivity = { 0.0f, 0.0f, 0.0f, 0.0f };

	D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
};

class Resource
{

public:

	constexpr static char	RAW_TEXTURE_NAME[] = "__DX_MOVIE_TEXTURE__";
	constexpr static char	FONT_TEXTURE_NAME[] = "conchars";

	constexpr static char	PROBE_STRUCTURED_BUFFER_NAME[] = "__DIFFUSE_PROBES_STRUCTURED_BUFFER__";
	constexpr static char	CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME[] = "__CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME__";


public:

	// When a job is started it expects resource to be in this state.
	// When a job is finished it leaves resource in this state.
	//#PERF parsing of visibility can allow to have appropriate state like D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE instead of this generic one
	const static D3D12_RESOURCE_STATES DEFAULT_STATE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

	Resource() = default;

	Resource(const Resource&) = delete;
	Resource& operator=(const Resource&) = delete;

	Resource(Resource&& other);
	Resource& operator=(Resource&& other);

	~Resource();
	
	static int BytesPerPixelFromFormat(DXGI_FORMAT format);
	static int GetBytesPerPixel(const ResourceDesc& desc);

public:

	ComPtr<ID3D12Resource> buffer;

	std::string name;
	
	ResourceDesc desc;
};


struct TexCreationRequest_FromFile
{
	explicit TexCreationRequest_FromFile(Resource& val) :
		texture(val)
	{}

	Resource& texture;
};

struct TexCreationRequest_FromData
{
	explicit TexCreationRequest_FromData(Resource& val) :
		texture(val)
	{}

	Resource& texture;
	std::optional<XMFLOAT4> clearValue;

	std::vector<std::byte> data;
};

/*
	I need to manage state of internal resources. I can't keep 
	this data inside Resource class itself, because multiple jobs might work
	on this resource in the same time, so what I am going to do is just have this
	proxy object that every frame will use to access it's internal resource
	NOTE: this is only intended for internal resources. 
*/
struct ResourceProxy
{
	explicit ResourceProxy(ID3D12Resource& initResource);
	ResourceProxy(ID3D12Resource& initResource, D3D12_RESOURCE_STATES initState);

	void TransitionTo(D3D12_RESOURCE_STATES newSate, ID3D12GraphicsCommandList* commandList);
	
	void static FindAndTranslateTo(
		const std::string& name,
		std::vector<ResourceProxy>& proxies,
		D3D12_RESOURCE_STATES newSate,
		ID3D12GraphicsCommandList* commandList);
	
	ID3D12Resource& resource;
	unsigned int hashedName = Const::INVALID_HASHED_NAME;

private:

	D3D12_RESOURCE_STATES state = Resource::DEFAULT_STATE;
};

using TextureCreationRequest_t = std::variant<TexCreationRequest_FromData, TexCreationRequest_FromFile>;