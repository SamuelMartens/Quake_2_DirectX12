#pragma once

#include <string>
#include <d3d12.h>

#include "dx_common.h"

class GraphicalObject
{
public:

	static constexpr int INVALID_OFFSET = -1;

	GraphicalObject() = default;

	GraphicalObject(const GraphicalObject&) = delete;
	GraphicalObject& operator=(const GraphicalObject&) = delete;

	GraphicalObject(GraphicalObject&& other);
	GraphicalObject& operator=(GraphicalObject&& other);

	std::string textureKey;
	ComPtr<ID3D12Resource> vertexBuffer;
	XMFLOAT4 position = {0.0f, 0.0f, 0.0f, 1.0f};

	int constantBufferOffset = INVALID_OFFSET;

	~GraphicalObject();
};