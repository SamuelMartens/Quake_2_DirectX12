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

// Taken from Q2-Pathtracing CalcReflectivityForPathtracing()
// basically averaging albedo. 
XMFLOAT4 AreaLight::CalculateReflectivity(const Resource& texture, const std::byte* textureData)
{
	// Not sure how relevant this is. Leaving this here just for remainder sake
	/* The reflectivity is only relevant for wall textures. */
	//if (image->type != it_wall)
		//return;

	DX_ASSERT(textureData != nullptr && "Invalid texture data");
	DX_ASSERT(texture.desc.dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && "Unknown texture dimension");
	DX_ASSERT(texture.desc.format == DXGI_FORMAT_R8G8B8A8_UNORM && "Invalid texture format");

	XMFLOAT4 reflectivity = { 0.0f, 0.0f, 0.0f, 0.0f };

	const int numTexels = texture.desc.height * texture.desc.width;

	for (int texelInd = 0; texelInd < numTexels; ++texelInd)
	{
		reflectivity.x += std::to_integer<int>(textureData[texelInd * 4 + 0]);
		reflectivity.y += std::to_integer<int>(textureData[texelInd * 4 + 1]);
		reflectivity.z += std::to_integer<int>(textureData[texelInd * 4 + 2]);
	}

	// Find average and then normalize
	XMStoreFloat4(&reflectivity,
		XMLoadFloat4(&reflectivity) / numTexels / 255.0f);

	// scale the reflectivity up, because the textures are
	// so dim
	float scale = ColorNormalize(reflectivity, reflectivity);

	if (scale < 0.5f)
	{
		scale *= 2.0f;

		XMStoreFloat4(&reflectivity,
			XMLoadFloat4(&reflectivity) * scale);
	}

	return reflectivity;
}
//#DEBUG rework radiance knowing that reflectivity is only suitable for radiosity method
XMFLOAT4 AreaLight::CalculateRadiance(const AreaLight& light)
{
	DX_ASSERT(light.staticObjectIndex != Const::INVALID_INDEX && "Invalid object index in light data");
	DX_ASSERT(light.area != 0.0f && "Invalid are in light data");

	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[light.staticObjectIndex];
	const Resource* lightTexture = ResourceManager::Inst().FindResource(object.textureKey);

	DX_ASSERT(lightTexture != nullptr && "Invalid texture name");

	XMFLOAT4 radiance;

	XMStoreFloat4(&radiance, 
		XMLoadFloat4(&lightTexture->desc.reflectivity) * lightTexture->desc.iradiance / (light.area * M_PI));

	return radiance;
}