#include "dx_objects.h"

#include <limits>

#include "dx_app.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

GraphicalObject::GraphicalObject(GraphicalObject&& other)
{
	if (this == &other)
	{
		return;
	}

	textureKey = std::move(other.textureKey);
	vertexBuffer = other.vertexBuffer;
	indexBuffer = other.indexBuffer;

	position = other.position;

	constantBufferOffset = other.constantBufferOffset;

	bbMin = std::move(other.bbMin);
	bbMax = std::move(other.bbMax);
	
	other.constantBufferOffset = INVALID_OFFSET;
}


void GraphicalObject::GenerateBoundingBox(const std::vector<XMFLOAT4>& vertices)
{
	constexpr float minFloat = std::numeric_limits<float>::min();
	constexpr float maxFloat = std::numeric_limits<float>::max();

	bbMax = XMFLOAT4( minFloat, minFloat, minFloat, 1.0f );
	bbMin = XMFLOAT4( maxFloat, maxFloat, maxFloat, 1.0f );

	for (const XMFLOAT4& vertex : vertices)
	{
		bbMax.x = std::max(bbMax.x, vertex.x);
		bbMax.y = std::max(bbMax.y, vertex.y);
		bbMax.z = std::max(bbMax.z, vertex.z);

		bbMin.x = std::min(bbMin.x, vertex.x);
		bbMin.y = std::min(bbMin.y, vertex.y);
		bbMin.z = std::min(bbMin.z, vertex.z);
	}
}

XMMATRIX GraphicalObject::GenerateModelMat() const
{
	XMMATRIX modelMat = XMMatrixScaling(scale.x, scale.y, scale.z);
	modelMat = modelMat * XMMatrixTranslation(
		position.x,
		position.y,
		position.z
	);

	return modelMat;
}

GraphicalObject& GraphicalObject::GraphicalObject::operator=(GraphicalObject&& other)
{
	if (this == &other)
	{
		return *this;
	}

	textureKey = std::move(other.textureKey);
	vertexBuffer = other.vertexBuffer;
	indexBuffer = other.indexBuffer;
	position = other.position;
	constantBufferOffset = other.constantBufferOffset;
	bbMin = std::move(other.bbMin);
	bbMax = std::move(other.bbMax);

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
