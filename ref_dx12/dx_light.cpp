#include "dx_light.h"

#include <numeric>

#include "dx_app.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif


namespace
{
	float ColorNormalize(const XMFLOAT4& in, XMFLOAT4& out)
	{
		const float max = std::max(std::max(in.x, in.y), in.z);

		if (max != 0.0f)
		{
			XMStoreFloat4(&out, XMLoadFloat4(&in) / max);
		}

		return max;
	}
}

void AreaLight::InitIfValid(AreaLight& light)
{
	DX_ASSERT(light.staticObjectIndex != Const::INVALID_INDEX && "Invalid object index for surface light init");

	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];

	const int trianglesNum = object.indices.size() / 3;

	DX_ASSERT(trianglesNum > 0 && "Invalid triangles num in GenerateObjectTrianglesPDF");

	std::vector<float> triangleAreas(trianglesNum, 0.0f);

	for (int triangleInd = 0; triangleInd < object.indices.size() / 3; ++triangleInd)
	{
		const int V0Ind = object.indices[triangleInd * 3 + 0];
		const int V1Ind = object.indices[triangleInd * 3 + 1];
		const int V2Ind = object.indices[triangleInd * 3 + 2];

		XMVECTOR sseV0 = XMLoadFloat4(&object.verticesPos[V0Ind]);
		XMVECTOR sseV1 = XMLoadFloat4(&object.verticesPos[V1Ind]);
		XMVECTOR sseV2 = XMLoadFloat4(&object.verticesPos[V2Ind]);

		triangleAreas[triangleInd] = XMVectorGetX(XMVector3Length(XMVector3Cross(sseV1 - sseV0, sseV2 - sseV0)) / 2);
	}

	light.area = std::accumulate(triangleAreas.cbegin(), triangleAreas.cend(), 0.0f);

	if (light.area == 0.0f)
	{
		return;
	}

	float currentSum = 0.0f;
	
	DX_ASSERT(light.trianglesPDF.empty() == true && "Light data should be empty during init");

	light.trianglesPDF.reserve(triangleAreas.size());

	std::transform(triangleAreas.cbegin(), triangleAreas.cend(), std::back_inserter(light.trianglesPDF),
		[&light, &currentSum](const float triangleArea)
	{
		currentSum += triangleArea;
		return currentSum / light.area;
	});

	DX_ASSERT(light.trianglesPDF.back() == 1.0f && "Something wrong with triangle PDF generation");

	light.radiance = CalculateRadiance(light);
}

float AreaLight::CalculateRadiance(const AreaLight& light)
{
	DX_ASSERT(light.staticObjectIndex != Const::INVALID_INDEX && "Invalid object index in light data");
	DX_ASSERT(light.area != 0.0f && "Invalid are in light data");

	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];
	const Resource* lightTexture = ResourceManager::Inst().FindResource(object.textureKey);

	DX_ASSERT(lightTexture != nullptr && "Invalid texture name");

	return lightTexture->desc.iradiance / (light.area * M_PI);
}