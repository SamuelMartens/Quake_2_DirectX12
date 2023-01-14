#pragma once

#include <tuple>

#include "dx_utils.h"
#include "dx_common.h"

extern "C"
{
	#include "../client/ref.h"
}

struct Camera
{
	void Init();

	void Update(const refdef_t& updateData);

	void GenerateViewProjMat();
	XMMATRIX GenerateViewMatrix() const;
	XMMATRIX GenerateProjectionMatrix() const;

	[[nodiscard]]
	XMMATRIX GetViewProjMatrix() const;

	// Yaw, Pitch , Roll
	std::tuple<XMFLOAT4, XMFLOAT4, XMFLOAT4> GetBasis() const;
	[[nodiscard]]
	std::array<Utils::Plane, 6> GetFrustumPlanes() const;
	
	// Result is in world space
	Utils::AABB GetAABB() const;
	// In degrees 
	XMFLOAT2 fov = { 0.0f, 0.0f };
	XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT3 viewangles = { 0.0f, 0.0f, 0.0f };

	int width = 0;
	int height = 0;

private:

	XMFLOAT4X4 viewProjMat;

};