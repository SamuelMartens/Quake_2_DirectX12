#include "dx_objects.h"

#include <limits>

#include "dx_app.h"
#include "dx_utils.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif


StaticObject::StaticObject(StaticObject&& other)
{
	*this = std::move(other);
}

StaticObject& StaticObject::StaticObject::operator=(StaticObject&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	textureKey = std::move(other.textureKey);
	
	verticesSizeInBytes = other.verticesSizeInBytes;
	other.verticesSizeInBytes = Const::INVALID_SIZE;

	indicesSizeInBytes = other.indicesSizeInBytes;
	other.indicesSizeInBytes = Const::INVALID_SIZE;

	vertices = other.vertices;
	other.vertices = Const::INVALID_BUFFER_HANDLER;

	indices = other.indices;
	other.indices = Const::INVALID_BUFFER_HANDLER;

	return *this;
}

std::tuple<XMFLOAT4, XMFLOAT4> StaticObject::GenerateAABB(const std::vector<XMFLOAT4>& vertices) const
{
	constexpr float minFloat = std::numeric_limits<float>::min();
	constexpr float maxFloat = std::numeric_limits<float>::max();

	XMVECTOR sseBBMin = XMVectorSet( maxFloat, maxFloat, maxFloat, 1.0f );
	XMVECTOR sseBBMax = XMVectorSet( minFloat, minFloat, minFloat, 1.0f );
	
	for (const XMFLOAT4& vertex : vertices)
	{
		const FXMVECTOR sseVertex = XMLoadFloat4(&vertex);

		sseBBMin = XMVectorMin(sseVertex, sseBBMin);
		sseBBMax = XMVectorMax(sseVertex, sseBBMax);
	}

	XMFLOAT4 bbMin, bbMax;

	XMStoreFloat4(&bbMin, sseBBMin);
	XMStoreFloat4(&bbMax, sseBBMax);

	return std::make_tuple(bbMin, bbMax);
}

StaticObject::~StaticObject()
{
	if (vertices != Const::INVALID_BUFFER_HANDLER)
	{
		Renderer::Inst().DeleteDefaultMemoryBuffer(vertices);
	}

	if (indices != Const::INVALID_BUFFER_HANDLER)
	{
		Renderer::Inst().DeleteDefaultMemoryBuffer(indices);
	}
}

DynamicObjectModel::DynamicObjectModel(DynamicObjectModel&& other)
{
	*this = std::move(other);
}

DynamicObjectModel& DynamicObjectModel::operator=(DynamicObjectModel&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	name = std::move(other.name);
	textures = std::move(other.textures);

	headerData = other.headerData;
	other.headerData.animFrameSizeInBytes = Const::INVALID_SIZE;
	other.headerData.animFrameVertsNum = Const::INVALID_SIZE;
	other.headerData.indicesNum = Const::INVALID_SIZE;

	textureCoords = other.textureCoords;
	other.textureCoords = Const::INVALID_BUFFER_HANDLER;

	vertices = other.vertices;
	other.vertices = Const::INVALID_BUFFER_HANDLER;

	indices = other.indices;
	other.indices = Const::INVALID_BUFFER_HANDLER;

	animationFrames = std::move(other.animationFrames);

	return *this;
}

XMMATRIX DynamicObjectModel::GenerateModelMat(const entity_t& entity)
{
	// -entity.angles[0] is intentional. Done to avoid some Quake shenanigans
	const XMFLOAT4 angles = XMFLOAT4(-entity.angles[0], entity.angles[1], entity.angles[2], 0.0f);

	// Quake 2 implementation of R_RotateForEntity 
	XMMATRIX sseModelMat =
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::axisX), XMConvertToRadians(-angles.z)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::axisY), XMConvertToRadians(-angles.x)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::axisZ), XMConvertToRadians(angles.y)) *

		XMMatrixTranslation(entity.origin[0], entity.origin[1], entity.origin[2]);

	return sseModelMat;
}

// move, front lerp, back lerp
std::tuple<XMFLOAT4, XMFLOAT4, XMFLOAT4> DynamicObjectModel::GenerateAnimInterpolationData(const entity_t& entity) const
{
	const DynamicObjectModel::AnimFrame& oldFrame = animationFrames[entity.oldframe];
	const DynamicObjectModel::AnimFrame& frame = animationFrames[entity.frame];

	XMVECTOR sseOldOrigin = XMLoadFloat4(&XMFLOAT4(entity.oldorigin[0], entity.oldorigin[1], entity.oldorigin[2], 1.0f));
	XMVECTOR sseOrigin = XMLoadFloat4(&XMFLOAT4(entity.origin[0], entity.origin[1], entity.origin[2], 1.0f));

	XMVECTOR sseDelta = XMVectorSubtract(sseOldOrigin, sseOrigin);

	const XMFLOAT4 angles = XMFLOAT4(entity.angles[0], entity.angles[1], entity.angles[2], 0.0f);

	// Generate anim transformation mat
	XMMATRIX sseRotationMat =
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::axisZ), XMConvertToRadians(-angles.y)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::axisY), XMConvertToRadians(-angles.x)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::axisX), XMConvertToRadians(-angles.z));

	// All we do here is transforming delta from world coordinates to model local coordinates
	XMVECTOR sseMove = XMVectorAdd(
		XMVector4Transform(sseDelta, sseRotationMat),
		XMLoadFloat4(&oldFrame.translate)
	);

	const float backLerp = entity.backlerp;

	XMFLOAT4 frontLerpVec = XMFLOAT4(1.0f - backLerp, 1.0f - backLerp, 1.0f - backLerp, 0.0f);
	XMFLOAT4 backLerpVec = XMFLOAT4(backLerp, backLerp, backLerp, 0.0f);

	sseMove = XMVectorMultiplyAdd(
		XMLoadFloat4(&backLerpVec),
		sseMove,
		XMVectorMultiply(XMLoadFloat4(&frontLerpVec), XMLoadFloat4(&frame.translate))
	);

	XMFLOAT4 move;
	XMStoreFloat4(&move, sseMove);


	XMVECTOR sseScaledFrontLerp = XMVectorMultiply(XMLoadFloat4(&frontLerpVec), XMLoadFloat4(&frame.scale));
	XMVECTOR sseScaledBackLerp = XMVectorMultiply(XMLoadFloat4(&backLerpVec), XMLoadFloat4(&oldFrame.scale));

	XMStoreFloat4(&frontLerpVec, sseScaledFrontLerp);
	XMStoreFloat4(&backLerpVec, sseScaledBackLerp);

	return std::make_tuple(move, frontLerpVec, backLerpVec);
}

DynamicObjectModel::~DynamicObjectModel()
{
	if (indices != Const::INVALID_BUFFER_HANDLER)
	{
		Renderer::Inst().DeleteDefaultMemoryBuffer(indices);
	}

	if (vertices != Const::INVALID_BUFFER_HANDLER)
	{
		Renderer::Inst().DeleteDefaultMemoryBuffer(vertices);
	}

	if (textureCoords != Const::INVALID_BUFFER_HANDLER)
	{
		Renderer::Inst().DeleteDefaultMemoryBuffer(textureCoords);
	}
}