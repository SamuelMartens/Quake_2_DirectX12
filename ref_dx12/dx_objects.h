#pragma once

#include <string>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <tuple>
#include <array>
#include <atomic>

#include "dx_common.h"
#include "dx_buffer.h"
#include "dx_settings.h"

extern "C" 
{
	#include "../client/ref.h"
};

// Contains source data in CPU accessible format
// For now used only in light baking
class SourceStaticObject
{
public:

	std::string textureKey;

	Utils::AABB aabb;

	std::vector<XMFLOAT4> verticesPos;
	std::vector<XMFLOAT4> normals;
	std::vector<XMFLOAT2> textureCoords;

	std::vector<int> indices;
};

// Contains only data required for rendering
class StaticObject
{
public:

	// IMPORTANT: don't forget to modify move constructor/assignment
	// when new members with limited lifetime is added to this class

	StaticObject() = default;

	StaticObject(const StaticObject&) = delete;
	StaticObject& operator=(const StaticObject&) = delete;

	StaticObject(StaticObject&& other);
	StaticObject& operator=(StaticObject&& other);

	~StaticObject();
	
	std::string textureKey;
	
	BufferHandler vertices = Const::INVALID_BUFFER_HANDLER;
	BufferHandler indices = Const::INVALID_BUFFER_HANDLER;

	int verticesSizeInBytes = Const::INVALID_SIZE;
	int indicesSizeInBytes = Const::INVALID_SIZE;
};

class DynamicObjectModel
{
public:

	// Derives from dmdl_t. 
	struct HeaderData
	{
		int animFrameVertsNum = Const::INVALID_SIZE;
		int animFrameNormalsNum = Const::INVALID_SIZE;
		int indicesNum = Const::INVALID_SIZE;
	};

	struct AnimFrame
	{
		XMFLOAT4 scale = {1.0f, 1.0f, 1.0f, 0.0f};
		XMFLOAT4 translate = { 0.0f, 0.0f, 0.0f, 0.0f };
		std::string name;
	};

	DynamicObjectModel() = default;

	DynamicObjectModel(const DynamicObjectModel&) = delete;
	DynamicObjectModel& operator=(const DynamicObjectModel&) = delete;

	DynamicObjectModel(DynamicObjectModel&& other);
	DynamicObjectModel& operator=(DynamicObjectModel&& other);

	static XMMATRIX GenerateModelMat(const entity_t& entity);

	 // move, front lerp, back lerp
	std::tuple<XMFLOAT4,XMFLOAT4,XMFLOAT4> GenerateAnimInterpolationData(const entity_t& entity) const;

	std::string name;

	// - A few textures is possible. So skins should be in vector
	std::vector<std::string> textures;

	// - Data from dmdl_t (stored in extradata)
	HeaderData headerData;

	// - Texture coordinates not interpolated stored in extradata as well
	BufferHandler textureCoords = Const::INVALID_BUFFER_HANDLER;

	// - Vertices, sequence of keyframe vertices
	BufferHandler vertices = Const::INVALID_BUFFER_HANDLER;

	BufferHandler normals = Const::INVALID_BUFFER_HANDLER;

	BufferHandler indices = Const::INVALID_BUFFER_HANDLER;

	// - Animation frames
	std::vector<AnimFrame> animationFrames;

	~DynamicObjectModel();
};

// Should match shader definition in Debug.passh
enum class DebugObjectType
{
	LightProbe = 0,
	LightSource = 1,
	ProbePathTraceSegment = 2,
	ProbeLightSample = 3,
	FrustumClusters = 4,

	None
};

struct DebugObject_ProbePathSegment
{
	int probeIndex = Const::INVALID_INDEX;
	int segmentIndex = Const::INVALID_INDEX;

	// This is stupid, but I must duplicate data here from original segment structure
	// otherwise it is not thread safe
	int bounce = -1;
};

struct DebugObject_LightSource
{
	// Should match shader definition in Debug.passh
	enum class Type
	{
		Area = 0,
		Point = 1,

		None
	};

	Type type = Type::None;
	int sourceIndex = Const::INVALID_INDEX;

	// For point lights only
	bool showRadius = false;
};

struct DebugObject_LightProbe
{
	int probeIndex = Const::INVALID_INDEX;
	XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct DebugObject_ProbeLightSample
{
	int probeIndex = Const::INVALID_INDEX;
	int pathIndex = Const::INVALID_INDEX;
	int pathPointIndex = Const::INVALID_INDEX;
	int sampleIndex = Const::INVALID_INDEX;

	XMFLOAT4 radiance =  { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct DebugObject_FrustumCluster
{
	int clusterIndex = Const::INVALID_INDEX;
};

// NOTE: when adding new type, add registration in Pass_Debug::RegisterObjects()
using DebugObject_t = std::variant<
	DebugObject_LightProbe,
	DebugObject_LightSource,
	DebugObject_ProbePathSegment,
	DebugObject_ProbeLightSample,
	DebugObject_FrustumCluster>;
