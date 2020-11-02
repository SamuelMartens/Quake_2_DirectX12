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

class StaticObjectFrameData
{
public:

	StaticObjectFrameData() = default;

	StaticObjectFrameData(const StaticObjectFrameData&) = delete;
	StaticObjectFrameData& operator=(const StaticObjectFrameData&) = delete;

	StaticObjectFrameData(StaticObjectFrameData&& other);
	StaticObjectFrameData& operator=(StaticObjectFrameData&& other);

	~StaticObjectFrameData();

	BufferHandler constantBufferHandler = BufConst::INVALID_BUFFER_HANDLER;
};

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
	// min, max
	std::tuple<XMFLOAT4, XMFLOAT4> GenerateAABB(const std::vector<XMFLOAT4>& vertices) const;

	std::string textureKey;
	
	BufferHandler vertices = BufConst::INVALID_BUFFER_HANDLER;
	BufferHandler indices = BufConst::INVALID_BUFFER_HANDLER;

	int verticesSizeInBytes = Const::INVALID_SIZE;
	int indicesSizeInBytes = Const::INVALID_SIZE;

	std::array<StaticObjectFrameData, Settings::FRAMES_NUM> frameData;
};

class DynamicObjectModel
{
public:

	// Derives from dmdl_t. 
	struct HeaderData
	{
		int animFrameSizeInBytes = Const::INVALID_SIZE;
		int animFrameVertsNum = Const::INVALID_SIZE;
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
	BufferHandler textureCoords = BufConst::INVALID_BUFFER_HANDLER;

	// - Vertices, sequence of keyframe vertices
	BufferHandler vertices = BufConst::INVALID_BUFFER_HANDLER;

	BufferHandler indices = BufConst::INVALID_BUFFER_HANDLER;

	// - Animation frames
	std::vector<AnimFrame> animationFrames;

	~DynamicObjectModel();
};


class DynamicObjectConstBuffer
{
public:
	DynamicObjectConstBuffer() = default;

	DynamicObjectConstBuffer(const DynamicObjectConstBuffer&) = delete;
	DynamicObjectConstBuffer& operator=(const DynamicObjectConstBuffer&) = delete;

	DynamicObjectConstBuffer(DynamicObjectConstBuffer&& other);
	DynamicObjectConstBuffer& operator=(DynamicObjectConstBuffer&& other);

	~DynamicObjectConstBuffer();

	BufferHandler constantBufferHandler = BufConst::INVALID_BUFFER_HANDLER;
	std::atomic<bool> isInUse = false;
};

// Temporary object. Should exist only for one draw call
struct DynamicObject
{
	DynamicObject(DynamicObjectModel* newModel, DynamicObjectConstBuffer* newConstBuffer) :
		model(newModel),
		constBuffer(newConstBuffer)
	{
		assert(constBuffer != nullptr && "Dynamic object cannot be created with null const buffer");
		assert(constBuffer->isInUse == true && "Const buffer must be allocated when dynamic object is created");
	};

	DynamicObject(const DynamicObject&) = delete;
	DynamicObject& operator=(const DynamicObject&) = delete;

	DynamicObject(DynamicObject&&);
	DynamicObject& operator=(DynamicObject&&);

	~DynamicObject();

	DynamicObjectModel* model = nullptr;
	DynamicObjectConstBuffer* constBuffer = nullptr;
};
