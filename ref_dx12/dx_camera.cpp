#include "dx_camera.h"
#include "dx_app.h"


#ifdef max
#undef max
#endif  

#ifdef min
#undef min
#endif  

const float Camera::Z_NEAR = 4.0f;
const float Camera::Z_FAR = 4065.0f;

namespace
{
	XMVECTOR XM_CALLCONV ScreenToView(FXMVECTOR screenPosition, FXMMATRIX inverseProjectionMat, int screenWidth, int screenHeight)
	{
		// Notice vector set has some one. This is to avoid division by 0. We are only interested in xy members of vector
		XMVECTOR ndcPosition = screenPosition / XMVectorSet(screenWidth, screenHeight, 1.0f, 1.0f);
		ndcPosition = ndcPosition * XMVectorSet(2.0f, -2.0f, 1.0f, 1.0f) + XMVectorSet(-1.0f, 1.0f, 0.0f, 0.0f);

		XMVECTOR viewSpace = XMVector4Transform(ndcPosition, inverseProjectionMat);
		viewSpace = viewSpace / XMVectorGetW(viewSpace);

		return viewSpace;
	}
}

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

XMMATRIX XM_CALLCONV Camera::GetViewProjMatrix() const
{
	return XMLoadFloat4x4(&viewProjMat);
}
//#DEBUG
#pragma optimize("", off)
//END

std::vector<Utils::AABB> Camera::GenerateFrustumClusterInViewSpace(int tileWidth, int tileHeight, int slicesNum) const
{
	DX_ASSERT(width != 0 && height != 0 && "Camera has invalid data");

	DX_ASSERT(width % tileWidth == 0 && "Tile width is incorrect");
	DX_ASSERT(height % tileHeight == 0 && "Tile height is incorrect ");
	
	XMVECTOR sseEyePosition = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
	
	XMVECTOR sseDeterminant;
	XMMATRIX sseInverseProjection = XMMatrixInverse(&sseDeterminant, GenerateProjectionMatrix());

	DX_ASSERT(XMVectorGetX(sseDeterminant) != 0.0f && "Invalid matrix determinant");

	const int tilesNumX = width / tileWidth;
	const int tilesNumY = height / tileHeight;
	const int tilesNumZ = slicesNum;

	std::vector<Utils::AABB> frustumClusters;
	frustumClusters.reserve(tilesNumX * tilesNumY * tilesNumZ);

	for (int xClusterIndex = 0; xClusterIndex < tilesNumX; ++xClusterIndex)
	{
		for (int yClusterIndex = 0; yClusterIndex < tilesNumY; ++yClusterIndex)
		{
			const XMVECTOR seeMinPointScreenSpace = XMVectorSet(
				xClusterIndex * tileWidth,
				yClusterIndex * tileHeight,
				1.0f,
				1.0f);

			const XMVECTOR seeMaxPointScreenSpace = XMVectorSet(
				(xClusterIndex + 1) * tileWidth,
				(yClusterIndex + 1) * tileHeight,
				1.0f,
				1.0f);

			const XMVECTOR sseMinPointViewSpace = ScreenToView(seeMinPointScreenSpace,
				sseInverseProjection, width, height);

			const XMVECTOR sseMaxPointViewSpace = ScreenToView(seeMaxPointScreenSpace,
				sseInverseProjection, width, height);

			for (int zClusterIndex = 0; zClusterIndex < tilesNumZ; ++zClusterIndex)
			{
				const float clusterNear = -Z_NEAR * std::powf(Z_FAR / Z_NEAR,
					zClusterIndex / static_cast<float>(tilesNumZ));

				const float clusterFar = -Z_NEAR * std::powf(Z_FAR / Z_NEAR,
					(zClusterIndex + 1) / static_cast<float>(tilesNumZ));

				const XMVECTOR sseTileNearPlane = XMVectorSet(0.0f, 0.0f, -1.0f, clusterNear);
				const XMVECTOR sseTileFarPlane = XMVectorSet(0.0f, 0.0f, -1.0f, clusterFar);

				const XMVECTOR sseMinPointNear = XMPlaneIntersectLine(sseTileNearPlane, sseEyePosition, sseMinPointViewSpace);
				const XMVECTOR sseMinPointFar = XMPlaneIntersectLine(sseTileFarPlane, sseEyePosition, sseMinPointViewSpace);
				const XMVECTOR sseMaxPointNear = XMPlaneIntersectLine(sseTileNearPlane, sseEyePosition, sseMaxPointViewSpace);
				const XMVECTOR sseMaxPointFar = XMPlaneIntersectLine(sseTileFarPlane, sseEyePosition, sseMaxPointViewSpace);

				const XMVECTOR sseAABBMin = XMVectorMin(
					XMVectorMin(sseMinPointNear, sseMinPointFar),
					XMVectorMin(sseMaxPointNear, sseMaxPointFar));

				const XMVECTOR sseAABBMax = XMVectorMax(
					XMVectorMax(sseMinPointNear, sseMinPointFar),
					XMVectorMax(sseMaxPointNear, sseMaxPointFar));

				Utils::AABB clusterAABB;
				XMStoreFloat4(&clusterAABB.minVert, sseAABBMin);
				XMStoreFloat4(&clusterAABB.maxVert, sseAABBMax);

				frustumClusters.push_back(clusterAABB);
			}
		}
	}

	return frustumClusters;
}

XMMATRIX XM_CALLCONV Camera::GenerateViewMatrix() const 
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

XMMATRIX XM_CALLCONV Camera::GenerateProjectionMatrix() const
{	
	// NOTE: Far and Near intentionally reversed for Perspective matrix
	return XMMatrixPerspectiveFovRH(XMConvertToRadians(std::max(fov.y, 1.0f)), width / height, Z_FAR, Z_NEAR);
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
	
	DX_ASSERT(XMVectorGetX(sseCameraTransformDeterminant) != 0.0f && "Camera transform inv can't be found. Determinant is zero");

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

int Camera::GetFrustumClustersNum() const
{
	const int clustersX = width / Camera::FRUSTUM_TILE_WIDTH;
	const int clustersY = height / Camera::FRUSTUM_TILE_HEIGHT;

	return clustersX * clustersY * Camera::FRUSTUM_CLUSTER_SLICES;
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
