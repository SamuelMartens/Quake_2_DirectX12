#pragma once

#include <tuple>
#include <vector>

#include "dx_utils.h"
#include "dx_common.h"

extern "C"
{
	#include "../client/ref.h"
}

struct Camera
{
public:

	static const float Z_NEAR;
	static const float Z_FAR;

	static const int FRUSTUM_TILE_WIDTH = 128;
	static const int FRUSTUM_TILE_HEIGHT = 128;
	static const int FRUSTUM_CLUSTER_SLICES = 5;

public:

	void Init();

	void Update(const refdef_t& updateData);

	void GenerateViewProjMat();
	[[nodiscard]]
	XMMATRIX XM_CALLCONV GenerateViewMatrix() const;
	[[nodiscard]]
	XMMATRIX XM_CALLCONV GenerateProjectionMatrix() const;

	[[nodiscard]]
	XMMATRIX XM_CALLCONV GetViewProjMatrix() const;

	[[nodiscard]]
	std::vector<Utils::AABB> GenerateFrustumClusterInViewSpace(int tileWidth, int tileHeight, int slicesNum) const;

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