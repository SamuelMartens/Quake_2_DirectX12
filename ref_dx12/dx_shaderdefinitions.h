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
	}

	namespace ConstBuff
	{
		// Building blocks -----------
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

		struct CameraData
		{
			XMFLOAT4 yaw;
			XMFLOAT4 pitch;
			XMFLOAT4 roll;
			XMFLOAT4 origin;
		};

		// Compositions ------------
		struct AnimInterpTranstMap
		{
			AnimInterp animInterp;
			TransMat transMat;
		};

		struct CameraDataTransMat
		{
			CameraData cameraData;
			TransMat transMat;
		};
		
	}
}