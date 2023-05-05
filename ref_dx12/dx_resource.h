#pragma once

#include <d3d12.h>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "d3dx12.h"
#include "dx_common.h"
#include "dx_utils.h"

struct SurfacePropertes
{
	int irradiance = 0;
	int flags = 0;
};

struct ResourceDesc
{
	int width = 0;
	int height = 0;

	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;

	std::optional<SurfacePropertes> surfaceProperties;
};

struct ResourceReadBackRequest
{
	// Which resource do we want to read
	std::string targetResourceName;
	// After which pass we shall perform read back
	std::string targetPassName;

	std::byte* readBackCPUMemory = nullptr;
};

class Resource
{

public:

	constexpr static char	RAW_TEXTURE_NAME[] = "__DX_MOVIE_TEXTURE__";
	constexpr static char	FONT_TEXTURE_NAME[] = "conchars";

	constexpr static char	PROBE_STRUCTURED_BUFFER_NAME[] = "__DIFFUSE_PROBES_STRUCTURED_BUFFER__";
	constexpr static char	CLUSTER_GRID_PROBE_STRUCTURED_BUFFER_NAME[] = "__CLUSTER_GRID_PROBE_STRUCTURED_BUFFER__";

	constexpr static char	FRUSTUM_CLUSTERS_AABB_NAME[] = "__FRUSTUM_CLUSTERS_AABB__";

	constexpr static char	LIGHT_LIST_NAME[] = "__LIGHT_LIST__";
	constexpr static char	LIGHT_BOUNDING_VOLUME_LIST_NAME[] = "__LIGHT_BOUNDING_VOLUME_LIST__";

	constexpr static char	DEBUG_PICKED_LIGHTS_LIST_NAME[] = "__DEBUG_PICKED_LIGHTS_LIST__";

	constexpr static char	CLUSTERED_LIGHTING_LIGHT_INDEX_GLOBAL_LIST_NAME[] = "__CLUSTERED_LIGHTING_LIGHT_INDEX_GLOBAL_LIST__";
	constexpr static char	CLUSTERED_LIGHTING_PER_CLUSTER_LIGH_DATA_NAME[] = "__CLUSTERED_LIGHTING_PER_CLUSTER_LIGHT_DATA__";
	constexpr static char	CLUSTERED_LIGHTING_LIGHT_CULLING_DATA_NAME[] = "__CLUSTERED_LIGHTING_LIGHT_CULLING_DATA__";

public:

	// When a job is started it expects resource to be in this state.
	// When a job is finished it leaves resource in this state.
	//#PERF parsing of visibility can allow to have appropriate state like D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE instead of this generic one
	const static D3D12_RESOURCE_STATES DEFAULT_STATE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	const static D3D12_RESOURCE_STATES DEFAULT_READBACK_STATE = D3D12_RESOURCE_STATE_COPY_DEST;

	Resource() = default;

	Resource(const Resource&) = delete;
	Resource& operator=(const Resource&) = delete; 

	Resource(Resource&& other);
	Resource& operator=(Resource&& other);

	~Resource();
	
	static int BytesPerPixelFromFormat(DXGI_FORMAT format);
	static int GetBytesPerPixel(const ResourceDesc& desc);
	static std::string GetReadbackResourceNameFromRequest(const ResourceReadBackRequest& request, int frameNumber);

public:

	ComPtr<ID3D12Resource> gpuBuffer;
	std::optional<std::vector<std::byte>> cpuBuffer;

	std::string name;
	
	ResourceDesc desc;
};


struct TexCreationRequest_FromFile
{
	explicit TexCreationRequest_FromFile(Resource& val) :
		texture(val)
	{}

	Resource& texture;
	bool saveResourceInCPUMemory = false;
};

struct ResourceCreationRequest_FromData
{
	explicit ResourceCreationRequest_FromData(Resource& val) :
		resource(val)
	{}

	Resource& resource;
	std::optional<XMFLOAT4> clearValue;
	bool saveResourceInCPUMemory = false;

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
	ResourceProxy(ID3D12Resource& initResource, D3D12_RESOURCE_STATES initState, D3D12_RESOURCE_STATES initInterPassState);

	void TransitionTo(D3D12_RESOURCE_STATES newSate, ID3D12GraphicsCommandList* commandList);
	
	void static FindAndTranslateTo(
		const std::string& name,
		std::vector<ResourceProxy>& proxies,
		D3D12_RESOURCE_STATES newSate,
		ID3D12GraphicsCommandList* commandList);
	
	ID3D12Resource& resource;
	unsigned int hashedName = Const::INVALID_HASHED_NAME;

	const std::optional<D3D12_RESOURCE_STATES> interPassState;

private:

	D3D12_RESOURCE_STATES state = Resource::DEFAULT_STATE;
};

using ResourceCreationRequest_t = std::variant<ResourceCreationRequest_FromData, TexCreationRequest_FromFile>;