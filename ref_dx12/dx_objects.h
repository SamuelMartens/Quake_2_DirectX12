#pragma once

#include <string>
#include <d3d12.h>
#include <memory>
#include <vector>

#include "dx_common.h"
#include "dx_buffer.h"

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
