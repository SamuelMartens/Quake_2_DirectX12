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
		DX_ASSERT(intensity > 0.0f && "Can't calculate light far distance, invalid intensity");
		DX_ASSERT(minDistance < maxDistance && "Can't calculate light far distance, invalid min/max");

		const float farDistance = minDistance / std::sqrt(Settings::DIRECT_LIGHT_INTENSITY_THRESHOLD / intensity);

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

	return lightTexture->desc.surfaceProperties->irradiance / (light.area * M_PI);
}

Utils::Sphere AreaLight::GetBoundingSphere(const AreaLight& light, const XMFLOAT2& extends, const XMFLOAT4& origin)
{
	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];

	DX_ASSERT(object.indices.empty() == false && "Empty area light source indices");
	DX_ASSERT(object.normals.empty() == false && "Empty area light source normals");

	const XMFLOAT4& normal = object.normals[0];

	const float falloffDist = CalculateLightFarDistance(light.radiance, 
		Settings::AREA_LIGHTS_MIN_DISTANCE,
		Settings::AREA_LIGHTS_MAX_DISTANCE);

	const float extendsLength = XMVectorGetX(XMVector2Length(XMLoadFloat2(&extends)));

	Utils::Sphere boundingVolume;
	boundingVolume.origin = origin;
	boundingVolume.radius = std::max(extendsLength, falloffDist);
	
	return boundingVolume;
}

GPULight AreaLight::ToGPULight(const AreaLight& light)
{
	// Find bound
	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];

	DX_ASSERT(object.indices.empty() == false && "Empty area light source indices");
	DX_ASSERT(object.normals.empty() == false && "Empty area light source normals");

	const XMFLOAT4& normal = object.normals[0];
	const XMVECTOR sseNormal = XMLoadFloat4(&normal);

	constexpr float epsilon = 0.00001f;

#if ENABLE_VALIDATION
	for (const XMFLOAT4& currentNormal : object.normals)
	{
		DX_ASSERT(XMVector4NearEqual(XMLoadFloat4(&currentNormal), sseNormal,
			XMVectorSet(epsilon, epsilon, epsilon, epsilon)) &&
			"In area light object all normal should be equal");
	}
#endif

	// Find translation as AABB center
	XMVECTOR sseMin = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 1.0f);
	XMVECTOR sseMax = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f);

	for (const int& index : object.indices)
	{
		XMVECTOR sseVertex = XMLoadFloat4(&object.verticesPos[index]);

		sseMin = XMVectorMin(sseMin, sseVertex);
		sseMax = XMVectorMax(sseMax, sseVertex);
	}

	const XMVECTOR sseCenter = (sseMin + sseMax) / 2.0f;
	const XMMATRIX sseTranslationMat = XMMatrixTranslation(-XMVectorGetX(sseCenter), -XMVectorGetY(sseCenter), -XMVectorGetZ(sseCenter));

	// Find Z alignment
	const float normalDotZAxis = XMVectorGetX(XMVector3Dot(sseNormal, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
	const XMFLOAT4 zAlignmentAxist = normalDotZAxis >= 0.0f ? XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f) : XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);

	// First of all align mesh with Z axis (i.e make them collinear). 
	XMFLOAT4X4 toAlignedZSpaceMatrix = Utils::ConstructV1ToV2RotationMatrix(normal, zAlignmentAxist);

	if (normalDotZAxis < 0.0f)
	{
		// In the end I want local matrix to transform normal to XMFLOAT4(0.0f, 0.0f, 1.0f, 0.0f),
		// ConstructV1ToV2RotationMatrix() works for normal rotation but it doesn't work perfectly for mesh rotation,
		// that's why we need this matrix to specifically flip aligned normal to Z axis
		XMStoreFloat4x4(&toAlignedZSpaceMatrix, XMLoadFloat4x4(&toAlignedZSpaceMatrix) * XMMatrixRotationY(M_PI));
	}

	// Find Y alignment, via longest edge
	XMFLOAT4 normalizedLongestEdge;
	{
		// I used fan triangulation to form my objects, so the outer edges are
		// 1) First edge of the first triangle
		// 2) Second edge of the each triangle
		// 3) Last edge of the last triangle

		// Init with first edge of the first triangle
		XMVECTOR sseLongestEdge = XMLoadFloat4(&object.verticesPos[object.indices[1]]) - XMLoadFloat4(&object.verticesPos[object.indices[0]]);
		float maxEdgeLengthSq = XMVectorGetX(XMVector3LengthSq(sseLongestEdge));

		// Check second edge of each triangle
		for (int i = 0; i < object.indices.size() / 3; i += 3)
		{
			const XMVECTOR sseEdge = XMLoadFloat4(&object.verticesPos[object.indices[i + 2]]) - XMLoadFloat4(&object.verticesPos[object.indices[i + 1]]);
			const float edgeLengthSq = XMVectorGetX(XMVector3LengthSq(sseEdge));

			if (edgeLengthSq > maxEdgeLengthSq)
			{
				sseLongestEdge = sseEdge;
				maxEdgeLengthSq = edgeLengthSq;
			}
		}

		// Check last edge of the last triangle
		{
			const XMVECTOR sseEdge = XMLoadFloat4(&object.verticesPos[object.indices[object.indices.size() - 1]]) -
				XMLoadFloat4(&object.verticesPos[object.indices[object.indices.size() - 2]]);

			const float edgeLengthSq = XMVectorGetX(XMVector3LengthSq(sseEdge));

			if (edgeLengthSq > maxEdgeLengthSq)
			{
				sseLongestEdge = sseEdge;
				maxEdgeLengthSq = edgeLengthSq;
			}
		}

		// Transform longest edge into Z Axis aligned space
		sseLongestEdge = XMVector4Transform(sseLongestEdge, XMLoadFloat4x4(&toAlignedZSpaceMatrix));

		const float edgeDotAlignmentAxis = XMVectorGetX(XMVector3Dot(sseLongestEdge, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
		
		// Make sure we don't flip mesh upside down accidentally
		if (edgeDotAlignmentAxis < 0.0f)
		{
			sseLongestEdge = sseLongestEdge * -1.0f;
		}

		XMStoreFloat4(&normalizedLongestEdge, XMVector3Normalize(sseLongestEdge));
	}

	// Find matrix to aligned Y
	const XMFLOAT4X4 toAlignedYSpaceMatrix = Utils::ConstructV1ToV2RotationMatrix(normalizedLongestEdge, XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f));

	// Now construct to the local space rotation matrix
	XMMATRIX sseToLocalTransformMatrix = XMLoadFloat4x4(&toAlignedZSpaceMatrix) * XMLoadFloat4x4(&toAlignedYSpaceMatrix);
	// Add translation to a local transformation 
	sseToLocalTransformMatrix = sseTranslationMat * sseToLocalTransformMatrix;

	// Find extends
	{
		sseMin = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 1.0f);
		sseMax = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f);

		for (const int& index : object.indices)
		{
			XMVECTOR sseVertex = XMLoadFloat4(&object.verticesPos[index]);
			sseVertex = XMVector4Transform(sseVertex, sseToLocalTransformMatrix);

			sseMin = XMVectorMin(sseMin, sseVertex);
			sseMax = XMVectorMax(sseMax, sseVertex);
		}
	}
	XMVECTOR sseExtends = sseMax - sseMin;

	GPULight gpuLight;
	gpuLight.type = static_cast<int>(Light::Type::Area);
	// Setting color as white for now. I might need to bring up reflectivity
	gpuLight.colorAndIntensity = XMFLOAT4(1.0f, 1.0f, 1.0f, light.radiance);
	XMStoreFloat2(&gpuLight.extends, sseExtends);

	DX_ASSERT(gpuLight.extends.x > 0.0f && gpuLight.extends.y > 0.0f && "Light extends must be bigger than 0.0f");

	XMVECTOR sseDeterminant;
	XMMATRIX sseToWorldSpaceMatrix = XMMatrixInverse(&sseDeterminant, sseToLocalTransformMatrix);
	DX_ASSERT(XMVectorGetX(sseDeterminant) != 0.0f && "Invalid matrix determinant");

	XMStoreFloat4x4(&gpuLight.worldTransform, sseToWorldSpaceMatrix);

	return gpuLight;
}

Utils::Sphere PointLight::GetBoundingSphere(const PointLight& light)
{
	Utils::Sphere sphere;

	sphere.origin = light.origin;
	sphere.radius = CalculateLightFarDistance(light.intensity, light.objectPhysicalRadius, Settings::POINT_LIGHTS_MAX_DISTANCE);

	return sphere;
}

GPULight PointLight::ToGPULight(const PointLight& light)
{
	GPULight gpuLight;
	gpuLight.type = static_cast<int>(Light::Type::Point);
	gpuLight.colorAndIntensity = XMFLOAT4(light.color.x, light.color.y, light.color.z, light.intensity);
	gpuLight.extends = XMFLOAT2(light.objectPhysicalRadius, 0.0f);
	
	XMStoreFloat4x4(&gpuLight.worldTransform, XMMatrixTranslation(light.origin.x, light.origin.y, light.origin.z));

	return gpuLight;
}

int Light::ClusteredLighting_GetGlobalLightIndicesElementsNum(int gpuLightsListSize)
{
	return gpuLightsListSize * Settings::CLUSTERED_LIGHTING_MAX_LIGHTS_PER_CLUSTER;
}
