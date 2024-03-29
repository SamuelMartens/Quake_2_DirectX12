#include "dx_utils.h"

#include <fstream>

#include "dx_app.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

const XMFLOAT4 Utils::AXIS_X = XMFLOAT4(1.0, 0.0, 0.0, 0.0);
const XMFLOAT4 Utils::AXIS_Y = XMFLOAT4(0.0, 1.0, 0.0, 0.0);
const XMFLOAT4 Utils::AXIS_Z = XMFLOAT4(0.0, 0.0, 1.0, 0.0);

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

	template<typename T>
	XMFLOAT4 InterpolateAttributeOfIntersection(const Utils::BSPNodeRayIntersectionResult& intersection, const std::vector<T>& attribute)
	{
		const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[intersection.staticObjIndex];

		const int v0Index = object.indices[intersection.triangleIndex * 3 + 0];
		const int v1Index = object.indices[intersection.triangleIndex * 3 + 1];
		const int v2Index = object.indices[intersection.triangleIndex * 3 + 2];

		XMVECTOR sseV0Attr = XMVectorZero();
		XMVECTOR sseV1Attr = XMVectorZero();
		XMVECTOR sseV2Attr = XMVectorZero();

		if constexpr (std::is_same_v<T, XMFLOAT4>)
		{
			sseV0Attr = XMLoadFloat4(&attribute[v0Index]);
			sseV1Attr = XMLoadFloat4(&attribute[v1Index]);
			sseV2Attr = XMLoadFloat4(&attribute[v2Index]);
		}
		else if constexpr (std::is_same_v<T, XMFLOAT2>)
		{
			sseV0Attr = XMLoadFloat2(&attribute[v0Index]);
			sseV1Attr = XMLoadFloat2(&attribute[v1Index]);
			sseV2Attr = XMLoadFloat2(&attribute[v2Index]);
		}
		else
		{
			DX_ASSERT(false && "Invalid type for attribute interpolation");
		}
	
		XMVECTOR sseInterpolatedAttr = sseV0Attr * intersection.rayTriangleIntersection.u +
			sseV1Attr * intersection.rayTriangleIntersection.v +
			sseV2Attr * intersection.rayTriangleIntersection.w;

		XMFLOAT4 result;
		XMStoreFloat4(&result, sseInterpolatedAttr);

		return result;
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
		1,0,4,  4,0,9,  4,9,5,  8,4,5,  1,4,8,
		1,8,10, 10,8,3, 8,5,3,  3,5,2,  3,2,7,
		3,7,10, 10,7,6, 6,7,11, 6,11,0, 6,0,1,
		10,6,1, 11,9,0, 2,9,11, 5,9,2,  11,7,2
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

XMFLOAT4X4 Utils::ConstructV1ToV2RotationMatrix(const XMFLOAT4& v1, const XMFLOAT4& v2)
{
	// Rodrigues' rotation formula

	XMVECTOR sseV1 = XMLoadFloat4(&v1);
	XMVECTOR sseV2 = XMLoadFloat4(&v2);

	DX_ASSERT(Utils::IsAlmostEqual(XMVectorGetX(XMVector3Length(sseV1)), 1.0f) && "V1 is not normalized");
	DX_ASSERT(Utils::IsAlmostEqual(XMVectorGetX(XMVector3Length(sseV2)), 1.0f) && "V2 is not normalized");

	XMVECTOR sseRotationAxis = XMVector3Cross(sseV1, sseV2);

	const float sin = XMVectorGetX(XMVector3Length(sseRotationAxis));
	const float cos = XMVectorGetX(XMVector3Dot(sseV1, sseV2));

	XMFLOAT4X4 rotationMatrix;

	if (Utils::IsAlmostEqual(sin, 0.0f) == true)
	{
		// V1 and V2 are collinear vectors, special handling is needed

		if (cos > 0.0f)
		{
			XMStoreFloat4x4(&rotationMatrix, XMMatrixIdentity());
		}
		else
		{
			XMStoreFloat4x4(&rotationMatrix, -1.0f * XMMatrixIdentity());
			rotationMatrix._44 = 1.0f;
		}
	}
	else
	{
		sseRotationAxis = XMVector3Normalize(sseRotationAxis);

		XMMATRIX sseRotationAxisMatrix = XMMatrixSet(
			0.0f, -XMVectorGetZ(sseRotationAxis), XMVectorGetY(sseRotationAxis), 0.0f,
			XMVectorGetZ(sseRotationAxis), 0.0f, -XMVectorGetX(sseRotationAxis), 0.0f,
			-XMVectorGetY(sseRotationAxis), XMVectorGetX(sseRotationAxis), 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);


		XMMATRIX sseRotationMatrix = XMMatrixIdentity() + sin * sseRotationAxisMatrix +
			(1.0f - cos) * sseRotationAxisMatrix * sseRotationAxisMatrix;


		XMStoreFloat4x4(&rotationMatrix, XMMatrixTranspose(sseRotationMatrix));

		// Got to manually correct this. It is fine if it is not equal to 1.0,
		// look at the final rotation matrix calculation, and see what possible value
		// can be here
		rotationMatrix._44 = 1.0f;
	}

	return rotationMatrix;
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
void Utils::VSCon_Print(const std::string& msg)
{
	OutputDebugStringA(msg.c_str());
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

std::vector<int> Utils::GetIndicesListForTrianglelistFromPolygonPrimitive(int numVertices)
{
	constexpr int TRIANGLE_VERT_NUM = 3;

	DX_ASSERT(numVertices >= TRIANGLE_VERT_NUM && "Invalid vert num for triangle list generation");

	std::vector<int> indices;
	indices.reserve((numVertices - 2) * TRIANGLE_VERT_NUM);

	constexpr int rootInd = 0;

	for (int i = rootInd + 1; i < numVertices - 1; ++i)
	{
		indices.push_back(rootInd);
		indices.push_back(i);
		indices.push_back(i + 1);
	}

	return indices;
}

std::vector<XMFLOAT4> Utils::GenerateNormals(const std::vector<XMFLOAT4>& vertices, const std::vector<int>& indices, std::vector<int>* degenerateTrianglesIndices)
{
	std::vector<XMFLOAT4> normals(vertices.size(), XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f));

	DX_ASSERT(indices.size() % 3 == 0 && "Invalid amount of indices");

	for (int triangleInd = 0; triangleInd < indices.size() / 3; ++triangleInd)
	{
		const int v0Ind = indices[triangleInd * 3 + 0];
		const int v1Ind = indices[triangleInd * 3 + 1];
		const int v2Ind = indices[triangleInd * 3 + 2];

		XMVECTOR sseV0 = XMLoadFloat4(&vertices[v0Ind]);
		XMVECTOR sseV1 = XMLoadFloat4(&vertices[v1Ind]);
		XMVECTOR sseV2 = XMLoadFloat4(&vertices[v2Ind]);

		XMVECTOR sseFaceCrossProd = XMVector3Cross(sseV2 - sseV0, sseV1 - sseV0);

		if (degenerateTrianglesIndices != nullptr)
		{
			const float crossLength = XMVectorGetX(XMVector3Length(sseFaceCrossProd));

			if (Utils::IsAlmostEqual(crossLength, 0.0f))
			{
				degenerateTrianglesIndices->push_back(triangleInd);
			}
		}

		XMStoreFloat4(&normals[v0Ind], XMLoadFloat4(&normals[v0Ind]) + sseFaceCrossProd);
		XMStoreFloat4(&normals[v1Ind], XMLoadFloat4(&normals[v1Ind]) + sseFaceCrossProd);
		XMStoreFloat4(&normals[v2Ind], XMLoadFloat4(&normals[v2Ind]) + sseFaceCrossProd);
	}

	for (XMFLOAT4& normal : normals)
	{
		XMStoreFloat4(&normal, XMVector3Normalize(XMLoadFloat4(&normal)));
		DX_ASSERT(normal.w == 0.0f && "Normals w should always be 0");
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

std::vector<XMFLOAT4> Utils::GenerateAABBVertices_LinePrimitveType(const AABB& aabb)
{
	constexpr int edgesNum = 12;
	constexpr int verticesPerEdge = 2;

	std::vector<XMFLOAT4> vertices;
	vertices.reserve(edgesNum * verticesPerEdge);

	const XMFLOAT4& min = aabb.minVert;
	const XMFLOAT4& max = aabb.maxVert;

	// Bottom
	vertices.push_back({ min.x, min.y, min.z, 1.0f });
	vertices.push_back({ max.x, min.y, min.z, 1.0f });

	vertices.push_back({ min.x, min.y, max.z, 1.0f });
	vertices.push_back({ max.x, min.y, max.z, 1.0f });

	vertices.push_back({ min.x, min.y, min.z, 1.0f });
	vertices.push_back({ min.x, min.y, max.z, 1.0f });

	vertices.push_back({ max.x, min.y, min.z, 1.0f });
	vertices.push_back({ max.x, min.y, max.z, 1.0f });

	// Top
	vertices.push_back({ min.x, max.y, min.z, 1.0f });
	vertices.push_back({ max.x, max.y, min.z, 1.0f });

	vertices.push_back({ min.x, max.y, max.z, 1.0f });
	vertices.push_back({ max.x, max.y, max.z, 1.0f });

	vertices.push_back({ min.x, max.y, min.z, 1.0f });
	vertices.push_back({ min.x, max.y, max.z, 1.0f });

	vertices.push_back({ max.x, max.y, min.z, 1.0f });
	vertices.push_back({ max.x, max.y, max.z, 1.0f });

	// Front
	vertices.push_back({ min.x, min.y, min.z, 1.0f });
	vertices.push_back({ min.x, max.y, min.z, 1.0f });

	vertices.push_back({ max.x, min.y, min.z, 1.0f });
	vertices.push_back({ max.x, max.y, min.z, 1.0f });

	// Back
	vertices.push_back({ min.x, min.y, max.z, 1.0f });
	vertices.push_back({ min.x, max.y, max.z, 1.0f });

	vertices.push_back({ max.x, min.y, max.z, 1.0f });
	vertices.push_back({ max.x, max.y, max.z, 1.0f });

	return vertices;
}

bool Utils::IsVectorNotZero(const XMFLOAT4& vector)
{
	return vector.x != 0.0f || vector.y != 0.0f || vector.z != 0.0f;
}

bool Utils::IsAlmostEqual(float a, float b)
{
	return std::abs(a - b) < Utils::EPSILON;
}

bool Utils::IsRayIntersectsAABB(const Ray& ray, const AABB& aabb, float* t)
{
	float tMin = -FLT_MAX;
	float tMax = FLT_MAX;

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
	if (tMin == -FLT_MAX)
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

	const float t = f * XMVectorGetX(XMVector3Dot(sseEdge2, sseQ));

	// Triangle is behind the ray
	if (t < 0.0f)
	{
		return false;
	}

	result.t = t;
	result.u = u;
	result.v = v;
	result.w = 1.0f - u - v;

	return true;
}

bool Utils::FindClosestIntersectionInNode(const Utils::Ray& ray, const BSPNode& node, Utils::BSPNodeRayIntersectionResult& result)
{
	float nodeIntersectionT = FLT_MAX;
	
	if (Utils::IsRayIntersectsAABB(ray, node.aabb, &nodeIntersectionT) == false ||
		nodeIntersectionT > result.rayTriangleIntersection.t)
	{
		return false;
	}

	const std::vector<SourceStaticObject>& objects = Renderer::Inst().GetSourceStaticObjects();

	float minRayT = FLT_MAX;

	for (const int objectIndex : node.objectsIndices)
	{
		float rayT = FLT_MAX;

		const SourceStaticObject& object = objects[objectIndex];
		// No intersection at all
		if (Utils::IsRayIntersectsAABB(ray, object.aabb, &rayT) == false)
		{
			continue;
		}

		// Potential intersection with object further than what we have, early reject
		if (rayT > minRayT)
		{
			continue;
		}

		DX_ASSERT(object.indices.size() % 3 == 0 && "Invalid triangle indices");

		for (int triangleIndex = 0; triangleIndex < object.indices.size() / 3; ++triangleIndex)
		{
			const XMFLOAT4& v0 = object.verticesPos[object.indices[triangleIndex * 3 + 0]];
			const XMFLOAT4& v1 = object.verticesPos[object.indices[triangleIndex * 3 + 1]];
			const XMFLOAT4& v2 = object.verticesPos[object.indices[triangleIndex * 3 + 2]];

			Utils::RayTriangleIntersectionResult rayTriangleResult;

			if (Utils::IsRayIntersectsTriangle(ray, v0, v1, v2, rayTriangleResult) == false)
			{
				continue;
			}

			if (rayTriangleResult.t > minRayT)
			{
				continue;
			}

			// Reject backface triangles. Dot product should be negative to make sure we hit the front side of triangle
			XMVECTOR sseV0Normal = XMLoadFloat4(&object.normals[object.indices[triangleIndex * 3]]);
			if (XMVectorGetX(XMVector3Dot(sseV0Normal, XMLoadFloat4(&ray.direction))) >= 0.0f)
			{
				continue;
			}


			minRayT = rayTriangleResult.t;

			result.rayTriangleIntersection = rayTriangleResult;
			result.staticObjIndex = objectIndex;
			result.triangleIndex = triangleIndex;
		}

	}

	if (minRayT != FLT_MAX)
	{
		return true;
	}

	return false;
}

float Utils::FindDistanceBetweenAABBs(const Utils::AABB& aabb1, const Utils::AABB& aabb2)
{
	const XMVECTOR sseAABB1Min = XMLoadFloat4(&aabb1.minVert);
	const XMVECTOR sseAABB1Max = XMLoadFloat4(&aabb1.maxVert);
	
	const XMVECTOR sseAABB2Min = XMLoadFloat4(&aabb2.minVert);
	const XMVECTOR sseAABB2Max = XMLoadFloat4(&aabb2.maxVert);

	const XMVECTOR sseAABB1Center = (sseAABB1Max + sseAABB1Min) / 2.0f;
	const XMVECTOR sseAABB1Extends = (sseAABB1Max - sseAABB1Min) / 2.0f;

	const XMVECTOR sseAABB2Center = (sseAABB2Max + sseAABB2Min) / 2.0f;
	const XMVECTOR sseAABB2Extends = (sseAABB2Max - sseAABB2Min) / 2.0f;

	const XMVECTOR closestPointsDist = XMVectorMax(XMVectorAbs(sseAABB2Center - sseAABB1Center) - (sseAABB1Extends + sseAABB2Extends), XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f));

	return XMVectorGetX(XMVector3Length(closestPointsDist));
}

int Utils::Find1DIndexFrom2D(XMINT2 sizeIn2D, XMINT2 coordsIn2D)
{
	//NOTE: the resource must be row major!
	return coordsIn2D.y * sizeIn2D.x + coordsIn2D.x;
}

XMFLOAT2 Utils::NomralizeWrapAroundTextrueCoordinates(const XMFLOAT2& texCoords)
{
	XMFLOAT2 normalizeTexCoords;

	normalizeTexCoords.x = texCoords.x - std::floor(texCoords.x);
	normalizeTexCoords.y = texCoords.y - std::floor(texCoords.y);

	if (normalizeTexCoords.x < 0.0f)
	{
		normalizeTexCoords.x = 1.0f - normalizeTexCoords.x;
	}

	if (normalizeTexCoords.y < 0.0f)
	{
		normalizeTexCoords.y = 1.0f - normalizeTexCoords.y;
	}

	return normalizeTexCoords;
}

XMFLOAT4 Utils::TextureBilinearSample(const std::vector<std::byte>& texture, DXGI_FORMAT textureFormat, int width, int height, XMFLOAT2 texCoords)
{
	const int bytesPerPixel = Resource::BytesPerPixelFromFormat(textureFormat);

	DX_ASSERT(texCoords.x >= 0.0f && texCoords.y >= 0.0f && "Invalid texture coordinates");
	DX_ASSERT(width > 0 && height > 0 && "Invalid tex size");
	DX_ASSERT(texture.size() == height * width * bytesPerPixel && "Invalid texture buffer size");

	float fXMin = 0.0f;
	float fYMin = 0.0f;

	const float xInterpolationFactor = std::modf(texCoords.x * (width - 1), &fXMin);
	const float yInterpolationFactor = std::modf(texCoords.y * (height - 1), &fYMin);

	const int xMin = static_cast<int>(fXMin);
	const int yMin = static_cast<int>(fYMin);

	const int xMax = std::min(xMin + 1, width - 1);
	const int yMax = std::min(yMin + 1, height - 1);

	auto samplePixelFunc = [bytesPerPixel, textureFormat](const std::vector<std::byte>& texture, int index)
	{
		XMFLOAT4 sample{0.0f, 0.0f, 0.0f, 0.0f};

		switch (textureFormat)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		{
			const uint8_t* pixelData = reinterpret_cast<const uint8_t*>(&texture[index * bytesPerPixel]);

			sample.x = pixelData[0] / 255.0f;
			sample.y = pixelData[1] / 255.0f;
			sample.z = pixelData[2] / 255.0f;
			sample.w = pixelData[3] / 255.0f;

			break;
		}
		default:
			DX_ASSERT(false && "Not implemented");
			break;
		}

		return sample;
	};

	constexpr int sampleQuadSize = 2;

	XMFLOAT4 sampledPixels[sampleQuadSize * sampleQuadSize];

	// Gather data
	for (int ySampleIndex = 0; ySampleIndex < sampleQuadSize; ++ySampleIndex)
	{
		for (int xSampleIndex = 0; xSampleIndex < sampleQuadSize; ++xSampleIndex)
		{
			const int xCoord = std::min(xSampleIndex + xMin, xMax);
			const int yCoord = std::min(ySampleIndex + yMin, yMax);

			const int textureBufferIndex = Find1DIndexFrom2D({ width, height }, { xCoord, yCoord });
			const int sampledPixelIndex = Find1DIndexFrom2D({ sampleQuadSize, sampleQuadSize }, { xSampleIndex, ySampleIndex });

			sampledPixels[sampledPixelIndex] = samplePixelFunc(texture, textureBufferIndex);
		}
	}

	// Now do interpolation
	XMFLOAT4 interpolatedPixelsX[sampleQuadSize];

	// Interpolate along X
	for (int ySampleIndex = 0; ySampleIndex < sampleQuadSize; ++ySampleIndex)
	{
		XMVECTOR sseV0 = XMLoadFloat4(&sampledPixels[Find1DIndexFrom2D({ sampleQuadSize, sampleQuadSize }, { 0, ySampleIndex })]);
		XMVECTOR sseV1 = XMLoadFloat4(&sampledPixels[Find1DIndexFrom2D({ sampleQuadSize, sampleQuadSize }, { 1, ySampleIndex })]);

		XMStoreFloat4(&interpolatedPixelsX[ySampleIndex], XMVectorLerp(sseV0, sseV1, xInterpolationFactor));
	}

	XMFLOAT4 result{0.0f, 0.0f, 0.0f, 0.0f};
	
	// Interpolate along Y for the final result
	XMStoreFloat4(&result, 
		XMVectorLerp(
			XMLoadFloat4(&interpolatedPixelsX[0]),
			XMLoadFloat4(&interpolatedPixelsX[1]),
			yInterpolationFactor));

	return result;
}

std::filesystem::path Utils::GetAbsolutePathToRootDir()
{
	// Ugly hacks time!
	const static std::string pathToThisFile = __FILE__;
	std::filesystem::path rootDirPath = 
		pathToThisFile.substr(0, pathToThisFile.rfind("\\"));

	return rootDirPath;
}

std::filesystem::path Utils::GenAbsolutePathToFile(const std::string& relativePath)
{
	return GetAbsolutePathToRootDir().append(relativePath);
}

std::string Utils::ReadFile(const std::filesystem::path& filePath)
{
	std::ifstream file(filePath);

	DX_ASSERT(file.is_open() == true && "Failed read a file");

	std::stringstream buffer;
	buffer << file.rdbuf();

	return buffer.str();
}

void Utils::WriteFile(const std::filesystem::path& filePath, const std::string& content)
{
	std::ofstream file(filePath);

	DX_ASSERT(file.is_open() == true && "Failed write a file");

	file << content;
}

XMFLOAT4 Utils::BSPNodeRayIntersectionResult::GetNormal(const Utils::BSPNodeRayIntersectionResult& result)
{
	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[result.staticObjIndex];

	XMFLOAT4 normal = InterpolateAttributeOfIntersection(result, object.normals);
	XMStoreFloat4(&normal, XMVector3Normalize(XMLoadFloat4(&normal)));

	return normal;
}

XMFLOAT2 Utils::BSPNodeRayIntersectionResult::GetTexCoord(const BSPNodeRayIntersectionResult& result)
{
	const SourceStaticObject& object = Renderer::Inst().GetSourceStaticObjects()[result.staticObjIndex];
	XMFLOAT4 texCoord = InterpolateAttributeOfIntersection(result, object.textureCoords);

	return { texCoord.x, texCoord.y };
}
