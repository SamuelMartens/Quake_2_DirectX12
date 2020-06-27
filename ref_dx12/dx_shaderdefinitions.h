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
	}

	namespace ConstBuff
	{
		struct TransMat
		{
			XMFLOAT4X4 transformationMat;
		};

		struct AnimInterp
		{
			XMFLOAT4 animMove;
			XMFLOAT4 frontLerp;
			XMFLOAT4 backLetp;
		};

		struct AnimInterpTranstMap
		{
			AnimInterp animInterp;
			TransMat transMat;
		};
	}
}