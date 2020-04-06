#include "dx_objects.h"

#include "dx_app.h"

GraphicalObject::GraphicalObject(GraphicalObject&& other)
{
	if (this == &other)
	{
		return;
	}

	textureKey = std::move(other.textureKey);
	vertexBuffer = other.vertexBuffer;
	position = other.position;
	constantBufferOffset = other.constantBufferOffset;
	
	other.constantBufferOffset = INVALID_OFFSET;
}

GraphicalObject& GraphicalObject::GraphicalObject::operator=(GraphicalObject&& other)
{
	if (this == &other)
	{
		return *this;
	}

	textureKey = std::move(other.textureKey);
	vertexBuffer = other.vertexBuffer;
	position = other.position;
	constantBufferOffset = other.constantBufferOffset;

	other.constantBufferOffset = INVALID_OFFSET;

	return *this;
}

GraphicalObject::~GraphicalObject()
{
	if (constantBufferOffset != -1)
	{
		Renderer::Inst().DeleteConstantBuffMemory(constantBufferOffset);
	}
}
