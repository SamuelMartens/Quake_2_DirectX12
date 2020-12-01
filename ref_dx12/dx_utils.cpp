#include "dx_utils.h"

#include <codecvt>
#include <stdarg.h> 
#include <cstddef>
#include <algorithm>
#include <cctype>

#include "dx_app.h"


const XMFLOAT4 Utils::axisX = XMFLOAT4(1.0, 0.0, 0.0, 0.0);
const XMFLOAT4 Utils::axisY = XMFLOAT4(0.0, 1.0, 0.0, 0.0);
const XMFLOAT4 Utils::axisZ = XMFLOAT4(0.0, 0.0, 1.0, 0.0);

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
