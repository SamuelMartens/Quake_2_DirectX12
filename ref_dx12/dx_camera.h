#pragma once

#include <tuple>

#include "dx_common.h"

extern "C"
{
	#include "../client/ref.h"
}

struct Camera
{
	void Update(const refdef_t& updateData);

	XMMATRIX GenerateViewMatrix() const;
	XMMATRIX GenerateProjectionMatrix() const;
	// Yaw, Pitch , Roll
	std::tuple<XMFLOAT4, XMFLOAT4, XMFLOAT4> GetBasis() const;

	XMFLOAT2 fov = { 0.0f, 0.0f };
	XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
	XMFLOAT3 viewangles = { 0.0f, 0.0f, 0.0f };

	int width = 0;
	int height = 0;
};