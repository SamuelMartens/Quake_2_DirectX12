#include "dx_camera.h"

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

	const XMFLOAT4 axisX = XMFLOAT4(1.0, 0.0, 0.0, 0.0);
	const XMFLOAT4 axisY = XMFLOAT4(0.0, 1.0, 0.0, 0.0);
	const XMFLOAT4 axisZ = XMFLOAT4(0.0, 0.0, 1.0, 0.0);

	XMMATRIX sseViewMat =
		XMMatrixTranslation(-position.x, -position.y, -position.z) *

		XMMatrixRotationAxis(XMLoadFloat4(&axisZ), XMConvertToRadians(-viewangles.y)) *
		XMMatrixRotationAxis(XMLoadFloat4(&axisY), XMConvertToRadians(-viewangles.x)) *
		XMMatrixRotationAxis(XMLoadFloat4(&axisX), XMConvertToRadians(-viewangles.z)) *

		XMMatrixRotationAxis(XMLoadFloat4(&axisZ), XMConvertToRadians(90.0f)) *
		XMMatrixRotationAxis(XMLoadFloat4(&axisX), XMConvertToRadians(-90.0f));

	return sseViewMat;

}

XMMATRIX Camera::GenerateProjectionMatrix() const
{
	constexpr int zNear = 4;
	constexpr int zFar = 4096;
	return XMMatrixPerspectiveFovRH(XMConvertToRadians(fov.y), width / height, zNear, zFar);
}
