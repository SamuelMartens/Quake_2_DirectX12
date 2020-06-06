#pragma once

#include <string>
#include <d3d12.h>
#include <memory>
#include <vector>

#include "dx_common.h"
#include "dx_buffer.h"

extern "C" 
{
	#include "../client/ref.h"
};

class GraphicalObject
{
public:

	// IMPORTANT: don't forget to modify move constructor/assignment
	// when new members with limited lifetime is added to this class

	static constexpr int INVALID_OFFSET = -1;

	GraphicalObject() = default;

	GraphicalObject(const GraphicalObject&) = delete;
	GraphicalObject& operator=(const GraphicalObject&) = delete;

	GraphicalObject(GraphicalObject&& other);
	GraphicalObject& operator=(GraphicalObject&& other);

	void GenerateBoundingBox(const std::vector<XMFLOAT4>& vertices);
	XMMATRIX GenerateModelMat() const;

	std::string textureKey;
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;

	XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 scale = { 1.0f, 1.0f, 1.0f, 0.0f };

	int constantBufferOffset = INVALID_OFFSET;

	// Bounding box
	XMFLOAT4 bbMax = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT4 bbMin = { 0.0f, 0.0f, 0.0f, 1.0f };;

	~GraphicalObject();
};

class DynamicGraphicalObject
{
public:
	// Derives from dmdl_t. 
	struct HeaderData
	{
		//#DEBUG do I need to keep it here?
		int animFrameSizeInBytes = -1;
		int animFrameVertsNum = -1;
	};

	struct AnimFrame
	{
		XMFLOAT3 scale = {1.0f, 1.0f, 1.0f};
		XMFLOAT3 translate = { 0.0f, 0.0f, 0.0f };
		std::string name;
	};

	DynamicGraphicalObject() = default;

	DynamicGraphicalObject(const DynamicGraphicalObject&) = delete;
	DynamicGraphicalObject& operator=(const DynamicGraphicalObject&) = delete;

	DynamicGraphicalObject(DynamicGraphicalObject&& other);
	DynamicGraphicalObject& operator=(DynamicGraphicalObject&& other);

	// - A few textures is possible. So skins should be in vector
	std::vector<std::string> textures;

	// - Data from dmdl_t (stored in extradata)
	HeaderData headerData;

	// - Texture coordinates not interpolated stored in extradata as well
	BufferHandler textureCoords = INVALID_BUFFER_HANDLER;

	// - Vertices, sequence of keyframe vertices
	BufferHandler vertices = INVALID_BUFFER_HANDLER;

	BufferHandler indices = INVALID_BUFFER_HANDLER;

	// - Animation frames
	std::vector<AnimFrame> animationFrames;

	~DynamicGraphicalObject();
};