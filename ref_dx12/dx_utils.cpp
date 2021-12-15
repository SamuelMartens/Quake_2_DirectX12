#include "dx_utils.h"

#include <codecvt>
#include <stdarg.h> 
#include <cstddef>
#include <algorithm>
#include <cctype>

#include "dx_app.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

const XMFLOAT4 Utils::axisX = XMFLOAT4(1.0, 0.0, 0.0, 0.0);
const XMFLOAT4 Utils::axisY = XMFLOAT4(0.0, 1.0, 0.0, 0.0);
const XMFLOAT4 Utils::axisZ = XMFLOAT4(0.0, 0.0, 1.0, 0.0);

namespace
{
	XMFLOAT4 MidPoint(const XMFLOAT4& v0, const XMFLOAT4& v1)
	{
		XMFLOAT4 midPoint;
		XMStoreFloat4(&midPoint,  0.5f * (XMLoadFloat4(&v0) + XMLoadFloat4(&v1)));

		return midPoint;
	}

	void Subdivide(std::vector<XMFLOAT4>& vertices)
	{
		// Save a copy of the input geometry.
		std::vector<XMFLOAT4> inputCopy = vertices;

		vertices.resize(0);

		//       v1
		//       *
		//      / \
		//     /   \
		//  m0*-----*m1
		//   / \   / \
		//  /   \ /   \
		// *-----*-----*
		// v0    m2     v2

		const int numTris = static_cast<int>(inputCopy.size() / 3);
		for (int i = 0; i < numTris; ++i)
		{
			XMFLOAT4 v0 = inputCopy[i * 3 + 0];
			XMFLOAT4 v1 = inputCopy[i * 3 + 1];
			XMFLOAT4 v2 = inputCopy[i * 3 + 2];

			//
			// Generate the midpoints.
			//

			XMFLOAT4 m0 = MidPoint(v0, v1);
			XMFLOAT4 m1 = MidPoint(v1, v2);
			XMFLOAT4 m2 = MidPoint(v0, v2);

			//
			// Add new geometry.
			//

			vertices.push_back(v0);
			vertices.push_back(m0);
			vertices.push_back(m2);

			vertices.push_back(m0);
			vertices.push_back(m1);
			vertices.push_back(m2);

			vertices.push_back(m2);
			vertices.push_back(m1);
			vertices.push_back(v2);

			vertices.push_back(m0);
			vertices.push_back(v1);
			vertices.push_back(m1);
		}
	}
}

std::vector<XMFLOAT4> Utils::CreateSphere(float radius, int numSubdivisions /*= 1*/)
{
	std::vector<XMFLOAT4> vertices;

	// Put a cap on the number of subdivisions.
	numSubdivisions = std::min<int>(numSubdivisions, 6);

	// Approximate a sphere by tessellating an icosahedron.

	const float X = 0.525731f;
	const float Z = 0.850651f;

	const XMFLOAT4 pos[12] =
	{
		XMFLOAT4(-X, 0.0f, Z, 1.0f),  XMFLOAT4(X, 0.0f, Z, 1.0f),
		XMFLOAT4(-X, 0.0f, -Z, 1.0f), XMFLOAT4(X, 0.0f, -Z, 1.0f),
		XMFLOAT4(0.0f, Z, X, 1.0f),   XMFLOAT4(0.0f, Z, -X, 1.0f),
		XMFLOAT4(0.0f, -Z, X, 1.0f),  XMFLOAT4(0.0f, -Z, -X, 1.0f),
		XMFLOAT4(Z, X, 0.0f, 1.0f),   XMFLOAT4(-Z, X, 0.0f, 1.0f),
		XMFLOAT4(Z, -X, 0.0f, 1.0f),  XMFLOAT4(-Z, -X, 0.0f, 1.0f)
	};

	constexpr int icosahedronVertsNum = 60;

	const int indices[icosahedronVertsNum] =
	{
		1,4,0,  4,9,0,  4,5,9,  8,5,4,  1,8,4,
		1,10,8, 10,3,8, 8,3,5,  3,2,5,  3,7,2,
		3,10,7, 10,6,7, 6,11,7, 6,0,11, 6,1,0,
		10,1,6, 11,0,9, 2,11,9, 5,2,9,  11,2,7
	};

	vertices.reserve(icosahedronVertsNum);

	for (const int index : indices)
	{
		vertices.push_back(pos[index]);
	}

	for (int i = 0; i < numSubdivisions; ++i)
	{
		Subdivide(vertices);
	}

	// Project vertices onto sphere and scale.
	for (int i = 0; i < vertices.size(); ++i)
	{
		// Project onto unit sphere.
		XMVECTOR n = XMVector4Normalize(XMVectorSetW(XMLoadFloat4(&vertices[i]), 0.0f));

		// Project onto sphere.
		XMVECTOR p = radius * n;

		XMStoreFloat4(&vertices[i], XMVectorSetW(p, 1.0f));
	}

	return vertices;
}

bool Utils::IsAABBBehindPlane(const Plane& plane, const AABB& aabb)
{
	XMVECTOR sseAABBMin = XMLoadFloat4(&aabb.minVert);
	XMVECTOR sseAABBMax = XMLoadFloat4(&aabb.maxVert);

	XMVECTOR sseAABBCenter = XMVectorDivide(XMVectorAdd(sseAABBMax, sseAABBMin), XMVectorSet(2.0f, 2.0f, 2.0f, 2.0f));
	XMVECTOR sseAABBPositiveHalfDiagonal = XMVectorDivide(XMVectorSubtract(sseAABBMax, sseAABBMin), XMVectorSet(2.0f, 2.0f, 2.0f, 2.0f));

	XMVECTOR ssePlaneNormal = XMLoadFloat4(&plane.normal);

	const float AABBExtent = XMVectorGetX(XMVector4Dot(XMVectorAbs(ssePlaneNormal), sseAABBPositiveHalfDiagonal));
	const float AABBCenterToPlaneDist = XMVectorGetX(XMVector4Dot(sseAABBCenter, ssePlaneNormal)) + plane.distance;

	return AABBCenterToPlaneDist - AABBExtent > 0;
}

Utils::Plane Utils::ConstructPlane(const XMFLOAT4& p0, const XMFLOAT4& p1, const XMFLOAT4& p2)
{
	// Point are expected to be clockwise
	Plane plane;

	XMVECTOR sseP1 = XMLoadFloat4(&p1);

	XMVECTOR sseNormal = XMVector4Normalize(XMVector3Cross(
		XMVectorSubtract(XMLoadFloat4(&p0), sseP1),
		XMVectorSubtract(XMLoadFloat4(&p2), sseP1)));
	
	XMStoreFloat4(&plane.normal, sseNormal);

	plane.distance = -1.0f * XMVectorGetX(XMVector4Dot(sseP1, sseNormal));
	
	return plane;
}

/*
=================
VSCon_Printf

Print to Visual Studio developers console
=================
*/
void Utils::VSCon_Printf(const char *msg, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start(argptr, msg);
	vsprintf_s(text, msg, argptr);
	va_end(argptr);

	OutputDebugStringA(text);
}


std::string Utils::StrToLower(const std::string& str)
{
	std::string res = str;

	std::transform(str.begin(), str.end(), res.begin(),
		[](unsigned char c) -> unsigned char { return std::tolower(c); });

	return res;
}

std::wstring Utils::StringToWideString(const std::string& s)
{
	// Taken from https://stackoverflow.com/a/27296/5008539
	int len = 0;
	int slength = (int)s.length() + 1;
	len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
	wchar_t* buf = new wchar_t[len];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
	std::wstring r(buf);
	delete[] buf;
	return r;
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error(char *error, ...)
{
	va_list		argptr;
	char		text[1024];
	char		msg[] = "%s";

	va_start(argptr, error);
	vsprintf(text, error, argptr);
	va_end(argptr);

	Renderer::Inst().GetRefImport().Sys_Error(ERR_FATAL, msg, text);
}

// This function is used in q_shared.c and I don't want to change anything in there,
// so it's ok, use Utils::Sprintf for everything else
void Com_Printf(char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	char		msg[] = "%s";

	va_start(argptr, fmt);
	vsprintf(text, fmt, argptr);
	va_end(argptr);

	Renderer::Inst().GetRefImport().Con_Printf(PRINT_ALL, msg, text);
}

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;

#endif

unsigned int Utils::Align(unsigned int size, unsigned int alignment)
{
	--alignment;
	return (size + alignment) & ~alignment;
}

void Utils::Sprintf(char* dest, int size, const char* fmt, ...)
{
	int		len;
	va_list		argptr;
	char	bigbuffer[0x1000];

	va_start(argptr, fmt);
	len = vsprintf(bigbuffer, fmt, argptr);
	va_end(argptr);

	assert(len < size);

	strncpy(dest, bigbuffer, size - 1);
}


void Utils::LoadPCX(char* filename, std::byte** image, std::byte** palette, int* width, int* height)
{
	byte** internalPalette = reinterpret_cast<byte**>(palette);
	byte** pic = reinterpret_cast<byte**>(image);

	byte	*raw;
	pcx_t	*pcx;
	int		x, y;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;

	*pic = NULL;


	const refimport_t& ri = Renderer::Inst().GetRefImport();

	//
	// load the file
	//
	len = ri.FS_LoadFile(filename, (void **)&raw);
	if (!raw)
	{
		char errorMsg[] = "Bad pcx file %s\n";

		ri.Con_Printf(PRINT_DEVELOPER, errorMsg, filename);
		return;
	}

	//
	// parse the PCX file
	//
	pcx = (pcx_t *)raw;

	pcx->xmin = LittleShort(pcx->xmin);
	pcx->ymin = LittleShort(pcx->ymin);
	pcx->xmax = LittleShort(pcx->xmax);
	pcx->ymax = LittleShort(pcx->ymax);
	pcx->hres = LittleShort(pcx->hres);
	pcx->vres = LittleShort(pcx->vres);
	pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
	pcx->palette_type = LittleShort(pcx->palette_type);

	raw = &pcx->data;

	if (pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| pcx->xmax >= 640
		|| pcx->ymax >= 480)
	{
		char errorMsg[] = "Bad pcx file %s\n";
		ri.Con_Printf(PRINT_ALL, errorMsg, filename);
		return;
	}

	out = (byte*)malloc((pcx->ymax + 1) * (pcx->xmax + 1));

	*pic = out;

	pix = out;

	if (internalPalette)
	{
		*internalPalette = (byte*)malloc(768);
		memcpy(*internalPalette, (byte *)pcx + len - 768, 768);
	}

	if (width)
		*width = pcx->xmax + 1;
	if (height)
		*height = pcx->ymax + 1;

	for (y = 0; y <= pcx->ymax; y++, pix += pcx->xmax + 1)
	{
		for (x = 0; x <= pcx->xmax; )
		{
			dataByte = *raw++;

			if ((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = *raw++;
			}
			else
				runLength = 1;

			while (runLength-- > 0)
				pix[x++] = dataByte;
		}

	}

	if (raw - (byte *)pcx > len)
	{
		char errorMsg[] = "PCX file %s was malformed";
		ri.Con_Printf(PRINT_DEVELOPER, errorMsg, filename);
		free(*pic);
		*pic = NULL;
	}

	ri.FS_FreeFile(pcx);
}

void Utils::LoadWal(char* filename, std::byte** image, int* width, int* height)
{
	miptex_t *mt = NULL;

	const refimport_t& ri = Renderer::Inst().GetRefImport();

	ri.FS_LoadFile(filename, (void **)&mt);
	if (!mt)
	{
		char errorMsg[] = "Bad pcx file %s\n";
		ri.Con_Printf(PRINT_ALL, errorMsg, filename);
		return;
	}

	*width = LittleLong(mt->width);
	*height = LittleLong(mt->height);
	int ofs = LittleLong(mt->offsets[0]);

	const int size = (*width) * (*height);

	byte* out = (byte*)malloc(size);
	memcpy(out, (byte *)mt + ofs, size);

	*image = (std::byte*)out;

	ri.FS_FreeFile((void *)mt);
}

void Utils::LoadTGA(char* filename, std::byte** image, int* width, int* height)
{
	int		columns, rows, numPixels;
	byte	*pixbuf;
	int		row, column;
	byte	*buf_p;
	byte	*buffer;
	int		length;
	TargaHeader		targa_header;
	byte			*targa_rgba;
	byte tmp[2];

	byte **pic = reinterpret_cast<byte**>(image);
	const refimport_t& ri = Renderer::Inst().GetRefImport();

	//
	// load the file
	//
	length = ri.FS_LoadFile(filename, (void **)&buffer);
	if (!buffer)
	{
		char errorMsg[] = "Bad tga file %s\n";
		ri.Con_Printf(PRINT_DEVELOPER, errorMsg, filename);
		return;
	}

	buf_p = buffer;

	targa_header.id_length = *buf_p++;
	targa_header.colormap_type = *buf_p++;
	targa_header.image_type = *buf_p++;

	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_index = LittleShort(*((short *)tmp));
	buf_p += 2;
	tmp[0] = buf_p[0];
	tmp[1] = buf_p[1];
	targa_header.colormap_length = LittleShort(*((short *)tmp));
	buf_p += 2;
	targa_header.colormap_size = *buf_p++;
	targa_header.x_origin = LittleShort(*((short *)buf_p));
	buf_p += 2;
	targa_header.y_origin = LittleShort(*((short *)buf_p));
	buf_p += 2;
	targa_header.width = LittleShort(*((short *)buf_p));
	buf_p += 2;
	targa_header.height = LittleShort(*((short *)buf_p));
	buf_p += 2;
	targa_header.pixel_size = *buf_p++;
	targa_header.attributes = *buf_p++;

	if (targa_header.image_type != 2
		&& targa_header.image_type != 10)
	{
		char errorMsg[] = "LoadTGA: Only type 2 and 10 targa RGB images supported\n";
		ri.Sys_Error(ERR_DROP, errorMsg);
	}

	if (targa_header.colormap_type != 0
		|| (targa_header.pixel_size != 32 && targa_header.pixel_size != 24))
	{
		char errorMsg[] = "LoadTGA: Only 32 or 24 bit images supported (no colormaps)\n";
		ri.Sys_Error(ERR_DROP, errorMsg);
	}

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	if (width)
		*width = columns;
	if (height)
		*height = rows;

	targa_rgba = (byte*)malloc(numPixels * 4);
	*pic = targa_rgba;

	if (targa_header.id_length != 0)
		buf_p += targa_header.id_length;  // skip TARGA image comment

	if (targa_header.image_type == 2) {  // Uncompressed, RGB images
		for (row = rows - 1; row >= 0; row--) {
			pixbuf = targa_rgba + row * columns * 4;
			for (column = 0; column < columns; column++) {
				unsigned char red, green, blue, alphabyte;
				switch (targa_header.pixel_size) {
				case 24:

					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = *buf_p++;
					green = *buf_p++;
					red = *buf_p++;
					alphabyte = *buf_p++;
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				}
			}
		}
	}
	else if (targa_header.image_type == 10) {   // Runlength encoded RGB images
		unsigned char red, green, blue, alphabyte, packetHeader, packetSize, j;
		for (row = rows - 1; row >= 0; row--) {
			pixbuf = targa_rgba + row * columns * 4;
			for (column = 0; column < columns; ) {
				packetHeader = *buf_p++;
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) {        // run-length packet
					switch (targa_header.pixel_size) {
					case 24:
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alphabyte = 255;
						break;
					case 32:
						blue = *buf_p++;
						green = *buf_p++;
						red = *buf_p++;
						alphabyte = *buf_p++;
						break;
					}

					for (j = 0; j < packetSize; j++) {
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						column++;
						if (column == columns) { // run spans across rows
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row * columns * 4;
						}
					}
				}
				else {                            // non run-length packet
					for (j = 0; j < packetSize; j++) {
						switch (targa_header.pixel_size) {
						case 24:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
						case 32:
							blue = *buf_p++;
							green = *buf_p++;
							red = *buf_p++;
							alphabyte = *buf_p++;
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
						}
						column++;
						if (column == columns) { // pixel packet run spans across rows
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row * columns * 4;
						}
					}
				}
			}
		breakOut:;
		}
	}

	ri.FS_FreeFile(buffer);
}

void Utils::MakeQuad(XMFLOAT2 posMin, XMFLOAT2 posMax, XMFLOAT2 texMin, XMFLOAT2 texMax, ShDef::Vert::PosTexCoord* outVert)
{

	// 
	//   1 +------------+ 2
	//     |            |
	//     |            |
	//     |            |
	//   0 +------------+ 3
	//

	ShDef::Vert::PosTexCoord vert0 = { XMFLOAT4(posMin.x, posMax.y, 0.0f, 1.0f), XMFLOAT2(texMin.x, texMax.y) };
	ShDef::Vert::PosTexCoord vert1 = { XMFLOAT4(posMin.x, posMin.y, 0.0f, 1.0f), XMFLOAT2(texMin.x, texMin.y) };
	ShDef::Vert::PosTexCoord vert2 = { XMFLOAT4(posMax.x, posMin.y, 0.0f, 1.0f), XMFLOAT2(texMax.x, texMin.y) };
	ShDef::Vert::PosTexCoord vert3 = { XMFLOAT4(posMax.x, posMax.y, 0.0f, 1.0f), XMFLOAT2(texMax.x, texMax.y) };


	outVert[0] = vert0;
	outVert[1] = vert1;
	outVert[2] = vert2;

	outVert[3] = vert0;
	outVert[4] = vert2;
	outVert[5] = vert3;
}

std::vector<uint32_t> Utils::GetIndicesListForTrianglelistFromPolygonPrimitive(int numVertices)
{
	constexpr int TRIANGLE_VERT_NUM = 3;

	assert(numVertices >= TRIANGLE_VERT_NUM && "Invalid vert num for triangle list generation");

	std::vector<uint32_t> indices;
	indices.reserve((numVertices - 2) * TRIANGLE_VERT_NUM);

	constexpr uint32_t rootInd = 0;

	for (uint32_t i = rootInd + 1; i < numVertices - 1; ++i)
	{
		indices.push_back(rootInd);
		indices.push_back(i);
		indices.push_back(i + 1);
	}

	return indices;
}

std::vector<XMFLOAT4> Utils::GenerateNormals(const std::vector<XMFLOAT4>& vertices, const std::vector<uint32_t>& indices)
{
	std::vector<XMFLOAT4> normals(vertices.size(), XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));

	assert(indices.size() % 3 == 0 && "Invalid amount of indices");

	for (int triangleInd = 0; triangleInd < indices.size() / 3; ++triangleInd)
	{
		const int v0Ind = indices[triangleInd * 3 + 0];
		const int v1Ind = indices[triangleInd * 3 + 1];
		const int v2Ind = indices[triangleInd * 3 + 2];

		XMVECTOR sseV0 = XMLoadFloat4(&vertices[v0Ind]);
		XMVECTOR sseV1 = XMLoadFloat4(&vertices[v1Ind]);
		XMVECTOR sseV2 = XMLoadFloat4(&vertices[v2Ind]);

		// Make sure direction is right, I might end up with normals facing inside,
		// in this case tweak order in which vertices are subtracted
		XMVECTOR sseFaceCrossProd = XMVector3Cross(sseV1 - sseV0, sseV2 - sseV0);

		XMStoreFloat4(&normals[v0Ind], XMLoadFloat4(&normals[v0Ind]) + sseFaceCrossProd);
		XMStoreFloat4(&normals[v1Ind], XMLoadFloat4(&normals[v1Ind]) + sseFaceCrossProd);
		XMStoreFloat4(&normals[v2Ind], XMLoadFloat4(&normals[v2Ind]) + sseFaceCrossProd);
	}

	for (XMFLOAT4& normal : normals)
	{
		normal.w = 1.0f;
		XMStoreFloat4(&normal, XMVector3Normalize(XMLoadFloat4(&normal)));
	}

	return normals;
}

Utils::AABB Utils::ConstructAABB(const std::vector<XMFLOAT4>& vertices)
{
	Utils::AABB aabb;

	XMVECTOR sseMin = XMLoadFloat4(&aabb.minVert);
	XMVECTOR sseMax = XMLoadFloat4(&aabb.maxVert);

	for (const XMFLOAT4& vertex : vertices)
	{
		XMVECTOR sseVertex = XMLoadFloat4(&vertex);

		sseMin = XMVectorMin(sseMin, sseVertex);
		sseMax = XMVectorMax(sseMax, sseVertex);
	}

	XMStoreFloat4(&aabb.minVert, sseMin);
	XMStoreFloat4(&aabb.maxVert, sseMax);

	return aabb;
}

bool Utils::IsVectorNotZero(const XMFLOAT4& vector)
{
	return vector.x != 0.0f || vector.y != 0.0f || vector.z != 0.0f;
}

bool Utils::IsRayIntersectsAABB(const Ray& ray, const AABB& aabb, float* t)
{
	float tMin = FLT_MAX;
	float tMax = -FLT_MAX;

	const float* rayDir = &ray.direction.x;
	const float* rayOrigin = &ray.origin.x;

	const float* aabbMin = &aabb.minVert.x;
	const float* aabbMax = &aabb.maxVert.x;

	for (int i = 0; i < 3; ++i)
	{
		if (rayDir[i] == 0.0f)
		{
			continue;
		}

		float currentTMin = (aabbMin[i] - rayOrigin[i]) / rayDir[i];
		float currentTMax = (aabbMax[i] - rayOrigin[i]) / rayDir[i];

		if (currentTMin > currentTMax)
		{
			std::swap(currentTMax, currentTMin);
		}

		tMin = std::max(tMin, currentTMin);
		tMax = std::min(tMax, currentTMax);

		// No intersection
		if (tMin > tMax)
		{
			return false;
		}

		// AABB is behind the ray
		if (tMax < 0)
		{
			return false;
		}
	}

	// In case we were not able to find ANY intersection
	if (tMin ==  FLT_MAX )
	{
		return false;
	}

	if (t != nullptr)
	{
		*t = tMin < 0.0f ? tMax : tMin;
	}
	
	return true;
}


bool Utils::IsRayIntersectsTriangle(const Ray& ray, const XMFLOAT4& v0, const XMFLOAT4& v1, const XMFLOAT4& v2, RayTriangleIntersectionResult& result)
{
	// Taken from Real-Time Rendering Second Edition, p581
	XMVECTOR sseV0 = XMLoadFloat4(&v0);
	XMVECTOR sseV1 = XMLoadFloat4(&v1);
	XMVECTOR sseV2 = XMLoadFloat4(&v2);

	XMVECTOR sseRayDir = XMLoadFloat4(&ray.direction);
	XMVECTOR sseRayOrigin = XMLoadFloat4(&ray.origin);

	XMVECTOR sseEdge1 = sseV1 - sseV0;
	XMVECTOR sseEdge2 = sseV2 - sseV0;

	XMVECTOR sseP = XMVector3Cross(sseRayDir, sseEdge2);
	const float a = XMVectorGetX(XMVector3Dot(sseEdge1, sseP));

	if (std::abs(a) < std::numeric_limits<float>::epsilon())
	{
		return false;
	}

	const float f = 1 / a;

	XMVECTOR sseS = sseRayOrigin - sseV0;

	const float u = f * XMVectorGetX(XMVector3Dot(sseS, sseP));

	if (u < 0.0f || u > 1.0f)
	{
		return false;
	}

	XMVECTOR sseQ = XMVector3Cross(sseS, sseEdge1);

	const float v = f * XMVectorGetX(XMVector3Dot(sseRayDir, sseQ));

	if (v < 0.0f || u + v > 1.0f)
	{
		return false;
	}

	result.t = f * XMVectorGetX(XMVector3Dot(sseEdge2, sseQ));
	result.u = u;
	result.v = v;
	result.w = 1.0f - u - v;

	return true;
}
