#include "dx_camera.h"
#include "dx_app.h"


#ifdef max
#undef max
#endif  

#ifdef min
#undef min
#endif  



void Camera::Init()
{
	Renderer::Inst().GetDrawAreaSize(&width, &height);

	XMStoreFloat4x4(&viewProjMat, XMMatrixIdentity());
}

void Camera::Update(const refdef_t& updateData)
{
	position.x = updateData.vieworg[0];
	position.y = updateData.vieworg[1];
	position.z = updateData.vieworg[2];

	viewangles.x = updateData.viewangles[0];
	viewangles.y = updateData.viewangles[1];
	viewangles.z = updateData.viewangles[2];

	fov.x = updateData.fov_x;
	fov.y = updateData.fov_y;

	width = updateData.width;
	height = updateData.height;
}

void Camera::GenerateViewProjMat()
{
	XMStoreFloat4x4(&viewProjMat, GenerateViewMatrix() * GenerateProjectionMatrix());
}

XMMATRIX Camera::GetViewProjMatrix() const
{
	return XMLoadFloat4x4(&viewProjMat);
}

XMMATRIX Camera::GenerateViewMatrix() const 
{
	// The reason we have this weird pre transformation here is the difference
	// between id's software coordinate system and DirectX coordinate system
	//
	// id's system:
	//	- X axis = Left/Right
	//	- Y axis = Forward/Backward
	//	- Z axis = Up/Down
	//
	// DirectX coordinate system:
	//	- X axis = Left/Right
	//	- Y axis = Up/Down
	//	- Z axis = Forward/Backward

	XMMATRIX sseViewMat =
		XMMatrixTranslation(-position.x, -position.y, -position.z) *

		XMMatrixRotationAxis(XMLoadFloat4(&Utils::AXIS_Z), XMConvertToRadians(-viewangles.y)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::AXIS_Y), XMConvertToRadians(-viewangles.x)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::AXIS_X), XMConvertToRadians(-viewangles.z)) *

		XMMatrixRotationAxis(XMLoadFloat4(&Utils::AXIS_Z), XMConvertToRadians(90.0f)) *
		XMMatrixRotationAxis(XMLoadFloat4(&Utils::AXIS_X), XMConvertToRadians(-90.0f));

	return sseViewMat;

}

XMMATRIX Camera::GenerateProjectionMatrix() const
{
	constexpr float zNear = 4.0f;
	constexpr float zFar = 4065.0f;
	
	// NOTE: Far and Near intentionally reversed for Perspective matrix
	return XMMatrixPerspectiveFovRH(XMConvertToRadians(std::max(fov.y, 1.0f)), width / height, zFar, zNear);
}

std::tuple<XMFLOAT4, XMFLOAT4, XMFLOAT4> Camera::GetBasis() const
{
	// Extract basis from view matrix
	XMFLOAT4X4 viewMat;
	XMStoreFloat4x4(&viewMat, XMMatrixTranspose(GenerateViewMatrix()));

	XMFLOAT4 yaw = *reinterpret_cast<XMFLOAT4*>(viewMat.m[1]);
	XMFLOAT4 pitch =* reinterpret_cast<XMFLOAT4*>(viewMat.m[0]);
	XMFLOAT4 roll;
	// Initially points left, we need right
	XMStoreFloat4(&roll, XMVectorScale(
		XMLoadFloat4(reinterpret_cast<XMFLOAT4*>(viewMat.m[2])),
		-1.0f));

	// Eliminate transpose factor
	yaw.w = pitch.w = roll.w = 0.0f;

	return std::make_tuple(yaw, pitch, roll);
}

std::array<Utils::Plane, 6> Camera::GetFrustumPlanes() const
{
	std::array<XMFLOAT4, 8> frustum = 
	{
		XMFLOAT4(-1.0f, -1.0f, 1.0f, 1.0f ),
		XMFLOAT4(-1.0f,  1.0f, 1.0f, 1.0f),
		XMFLOAT4(1.0f,  1.0f, 1.0f, 1.0f),
		XMFLOAT4(1.0f, -1.0f, 1.0f, 1.0f),
		XMFLOAT4(-1.0f, -1.0f, 0.0f, 1.0f),
		XMFLOAT4(-1.0f,  1.0f, 0.0f, 1.0f),
		XMFLOAT4(1.0f,  1.0f, 0.0f, 1.0f),
		XMFLOAT4(1.0f, -1.0f, 0.0f, 1.0f)
	};

	XMVECTOR sseCameraTransformDeterminant;
	
	const XMMATRIX sseCameraInvTransform = XMMatrixInverse(&sseCameraTransformDeterminant,
		XMMatrixMultiply(GenerateViewMatrix(), GenerateProjectionMatrix()));
	
	assert(XMVectorGetX(sseCameraTransformDeterminant) != 0.0f && "Camera transform inv can't be found. Determinant is zero");

	std::for_each(frustum.begin(), frustum.end(), [sseCameraInvTransform](XMFLOAT4& point)
	{
		XMVECTOR ssePoint = XMLoadFloat4(&point);

		ssePoint = XMVector4Transform(ssePoint, sseCameraInvTransform);
		const float w = XMVectorGetW(ssePoint);

		ssePoint = XMVectorDivide(ssePoint, XMVectorSet(w, w, w, w));

		XMStoreFloat4(&point, ssePoint);
	});

	return {
		Utils::ConstructPlane(frustum[3], frustum[0], frustum[1]), // near
		Utils::ConstructPlane(frustum[5], frustum[4], frustum[7]), // far

		Utils::ConstructPlane(frustum[7], frustum[3], frustum[2]), // right
		Utils::ConstructPlane(frustum[1], frustum[0], frustum[4]), // left

		Utils::ConstructPlane(frustum[2], frustum[1], frustum[5]), // top
		Utils::ConstructPlane(frustum[4], frustum[0], frustum[3]), // bottom
	};
}

Utils::AABB Camera::GetAABB() const
{
	std::array<XMVECTOR, 8> frustum = 
	{
		XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f ),
		XMVectorSet(-1.0f,  1.0f, 1.0f, 1.0f),
		XMVectorSet(1.0f,  1.0f, 1.0f, 1.0f),
		XMVectorSet(1.0f, -1.0f, 1.0f, 1.0f),
		XMVectorSet(-1.0f, -1.0f, 0.0f, 1.0f),
		XMVectorSet(-1.0f,  1.0f, 0.0f, 1.0f),
		XMVectorSet(1.0f,  1.0f, 0.0f, 1.0f),
		XMVectorSet(1.0f, -1.0f, 0.0f, 1.0f)
	};

	XMVECTOR sseCameraTransformDeterminant;
	const XMMATRIX sseCameraInvTransform = XMMatrixInverse(&sseCameraTransformDeterminant,
		XMMatrixMultiply(GenerateViewMatrix(), GenerateProjectionMatrix()));

	assert(XMVectorGetX(sseCameraTransformDeterminant) != 0.0f && "Camera transform inv can't be found. Determinant is zero");

	constexpr float MIN_FLOAT = -std::numeric_limits<float>::max();
	constexpr float MAX_FLOAT = std::numeric_limits<float>::max();

	XMVECTOR sseBBMin = XMVectorSet(MAX_FLOAT, MAX_FLOAT, MAX_FLOAT, 1.0f);
	XMVECTOR sseBBMax = XMVectorSet(MIN_FLOAT, MIN_FLOAT, MIN_FLOAT, 1.0f);

	std::for_each(frustum.cbegin(), frustum.cend(),
		[&sseBBMin, &sseBBMax, sseCameraInvTransform](const XMVECTOR& fPoint) 
	{
		XMVECTOR sseFPointInWorldSpace = XMVector4Transform(fPoint, sseCameraInvTransform);
		const float w = XMVectorGetW(sseFPointInWorldSpace);

		sseFPointInWorldSpace = XMVectorDivide(sseFPointInWorldSpace, XMVectorSet(w, w, w, w));

		sseBBMin = XMVectorMin(sseBBMin, sseFPointInWorldSpace);
		sseBBMax = XMVectorMax(sseBBMax, sseFPointInWorldSpace);
	});

	XMFLOAT4 bbMin, bbMax;
	XMStoreFloat4(&bbMin, sseBBMin);
	XMStoreFloat4(&bbMax, sseBBMax);

	return { bbMin, bbMax };
}
