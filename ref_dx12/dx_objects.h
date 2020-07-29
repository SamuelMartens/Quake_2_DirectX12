#pragma once

#include <string>
#include <d3d12.h>
#include <memory>
#include <vector>
#include <tuple>

#include "dx_common.h"
#include "dx_buffer.h"

extern "C" 
{
	#include "../client/ref.h"
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

	void GenerateBoundingBox(const std::vector<XMFLOAT4>& vertices);
	XMMATRIX GenerateModelMat() const;

	std::string textureKey;
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;

	XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 scale = { 1.0f, 1.0f, 1.0f, 0.0f };

	BufferHandler constantBufferHandler = BufConst::INVALID_BUFFER_HANDLER;

	// Bounding box
	XMFLOAT4 bbMax = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 bbMin = { 0.0f, 0.0f, 0.0f, 1.0f };;

	~StaticObject();
};

class DynamicObjectModel
{
public:

	// Derives from dmdl_t. 
	struct HeaderData
	{
		int animFrameSizeInBytes = -1;
		int animFrameVertsNum = -1;
		int indicesNum = -1;
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
	bool isInUse = false;
};

// Temporary object. Should exist only for one draw call
struct DynamicObject
{
	DynamicObject(DynamicObjectModel* newModel, DynamicObjectConstBuffer* newConstBuffer) :
		model(newModel),
		constBuffer(newConstBuffer)
	{
		assert(constBuffer != nullptr && "Dynamic object cannot be created with null const buffer");

		constBuffer->isInUse = true;
	};

	DynamicObject(const DynamicObject&) = delete;
	DynamicObject& operator=(const DynamicObject&) = delete;

	DynamicObject(DynamicObject&&);
	DynamicObject& operator=(DynamicObject&&);

	~DynamicObject();

	DynamicObjectModel* model = nullptr;
	DynamicObjectConstBuffer* constBuffer = nullptr;
};
