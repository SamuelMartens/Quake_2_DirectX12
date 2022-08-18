#pragma once

#include "dx_common.h"
// SD - shader definitions
namespace ShDef 
{
	namespace Vert
	{
		struct PosTexCoord
		{
			XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
			XMFLOAT2 texCoord = { 0.0f, 0.0f };
		};

		struct PosCol
		{
			XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
			XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 0.0f };
		};

		struct PosNormTexCoord
		{
			XMFLOAT4 position = { 0.0f, 0.0f, 0.0f, 1.0f };
			XMFLOAT4 normal = { 0.0f, 0.0f, 0.0f, 0.0f };
			XMFLOAT2 texCoord = { 0.0f, 0.0f };

		};

		using StaticObjVert_t = PosNormTexCoord;
	}
}