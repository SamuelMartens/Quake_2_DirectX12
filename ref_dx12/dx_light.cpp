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

	float CalculateLightFarDistance(float intensity, float minDistance, float maxDistance)
	{
		constexpr float intensityCutoff = 0.01f;

		DX_ASSERT(intensity > 0.0f && "Can't calculate light far distance, invalid intensity");
		DX_ASSERT(minDistance < maxDistance && "Can't calculate light far distance, invalid min/max");

		const float farDistance = minDistance / std::sqrt(intensityCutoff / intensity);

		return std::clamp(farDistance, minDistance, maxDistance);
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

	return lightTexture->desc.irradiance / (light.area * M_PI);
}

Utils::Hemisphere AreaLight::GetBoundingHemisphere(const AreaLight& light)
{
	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];

	DX_ASSERT(object.indices.empty() == false && "Empty area light source indices");
	DX_ASSERT(object.normals.empty() == false && "Empty area light source normals");

	const XMFLOAT4& normal = object.normals[0];

	constexpr float epsilon = 0.00001f;

#if ENABLE_VALIDATION
	for (const XMFLOAT4& currentNormal : object.normals)
	{
		DX_ASSERT(XMVector4NearEqual(XMLoadFloat4(&currentNormal), XMLoadFloat4(&normal),
			XMVectorSet(epsilon, epsilon, epsilon, epsilon)) &&
			"In area light object all normal should be equal");
	}
#endif

	const XMFLOAT4X4 toLocalSpaceMatrix = Utils::ConstructV1ToV2RotationMatrix(normal, XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f));
	const XMMATRIX sseToLocalSpaceMatrix = XMLoadFloat4x4(&toLocalSpaceMatrix);

	XMVECTOR sseMin = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 1.0f);
	XMVECTOR sseMax = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f);

	for (const int& index : object.indices)
	{
		XMVECTOR sseVertex = XMLoadFloat4(&object.verticesPos[index]);
		sseVertex = XMVector4Transform(sseVertex, sseToLocalSpaceMatrix);

		sseMin = XMVectorMin(sseMin, sseVertex);
		sseMax = XMVectorMax(sseMax, sseVertex);
	}

	XMVECTOR sseCenter = (sseMin + sseMax) / 2.0f;
	const float maxDist = XMVectorGetX(XMVector3Length((sseMax - sseMin) / 2.0f));

	const float falloffDist = CalculateLightFarDistance(light.radiance, 
		Settings::AREA_LIGHTS_MIN_DISTANCE,
		Settings::AREA_LIGHTS_MAX_DISTANCE);

	XMVECTOR sseDeterminant;

	XMMATRIX sseToWorldSpaceMatrix = XMMatrixInverse(&sseDeterminant, sseToLocalSpaceMatrix);
	DX_ASSERT(XMVectorGetX(sseDeterminant) != 0.0f && "Invalid matrix determinant");

	sseCenter = XMVector4Transform(sseCenter, sseToWorldSpaceMatrix);

	Utils::Hemisphere boundingVolume;
	XMStoreFloat4(&boundingVolume.origin, sseCenter);
	boundingVolume.normal = normal;
	boundingVolume.radius = std::max(maxDist, falloffDist);
	
#if ENABLE_VALIDATION
	{
		XMFLOAT4 min;
		XMFLOAT4 max;

		XMStoreFloat4(&min, sseMin);
		XMStoreFloat4(&max, sseMax);

		DX_ASSERT(min.x <= max.x && min.y <= max.y && min.z <= max.z && 
			"Minimum should be less than maximum");
	}
#endif

	return boundingVolume;
}

Utils::Sphere PointLight::GetBoundingSphere(const PointLight& light)
{
	Utils::Sphere sphere;

	sphere.origin = light.origin;
	sphere.radius = CalculateLightFarDistance(light.intensity, light.radius, Settings::POINT_LIGHTS_MAX_DISTANCE);

	return sphere;
}
