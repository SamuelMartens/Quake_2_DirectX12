
#include "dx_app.h"

extern "C"
{
	#include "../client/ref.h"
	#include "../ref_gl/gl_local.h"
}


/** Wrap C++ functionality in C interface **/
// Register map as the world
void DX12_BeginReg(char* map)
{}

struct model_s* DX12_RegModel(char* model)
{
	return NULL;
}

struct image_s* DX12_RegSkin(char* skin)
{
	return NULL;
}

struct image_s* DX12_Draw_FindPic(char* name)
{
	return NULL;
}

void DX12_SetSky (char* name, float rotate, vec3_t axis)
{}

void DX12_EndReg(void)
{}

void DX12_RenderFrame(refdef_t *fd)
{}

void DX12_Draw_GetPicSize(int *w, int *h, char *name)
{}

void DX12_Draw_Pic(int x, int y, char *name)
{}

void DX12_Draw_StretchPic(int x, int y, int w, int h, char *name)
{}

void DX12_Draw_Char(int x, int y, int c)
{}

void DX12_Draw_TileClear(int x, int y, int w, int h, char *name)
{}

void DX12_Draw_Fill(int x, int y, int w, int h, int c)
{}

void DX12_Draw_FadeScreen(void)
{}

void DX12_Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, byte *data)
{}

qboolean DX12_Init( void *hinstance, void *hWnd )
{
	DXApp::Inst().Init(
		reinterpret_cast<WNDPROC>(hWnd),
		reinterpret_cast<HINSTANCE>(hinstance)
	);

	return qTrue;
}

void DX12_Shutdown(void)
{}

void DX12_CinematicSetPalette()
{}

void DX12_BeginFrame( float camera_separation )
{}

void DX12_EndFrame(void)
{}

void DX12_SetPalette(const unsigned char *palette)
{}

void DX12_AppActivate( qboolean active )
{}

refexport_t GetRefAPI (refimport_t rimp)
{
	DXApp::Inst().SetRefImport(rimp);
	refexport_t re;

	re.api_version = API_VERSION;

	re.BeginRegistration = DX12_BeginReg;
	re.RegisterModel = DX12_RegModel;
	re.RegisterSkin = DX12_RegSkin;
	re.RegisterPic = DX12_Draw_FindPic;
	re.SetSky = DX12_SetSky;
	re.EndRegistration = DX12_EndReg;

	re.RenderFrame = DX12_RenderFrame;

	re.DrawGetPicSize = DX12_Draw_GetPicSize;
	re.DrawPic = DX12_Draw_Pic;
	re.DrawStretchPic = DX12_Draw_StretchPic;
	re.DrawChar = DX12_Draw_Char;
	re.DrawTileClear = DX12_Draw_TileClear;
	re.DrawFill = DX12_Draw_Fill;
	re.DrawFadeScreen = DX12_Draw_FadeScreen;

	re.DrawStretchRaw = DX12_Draw_StretchRaw;

	re.Init = DX12_Init;
	re.Shutdown = DX12_Shutdown;

	re.CinematicSetPalette = DX12_SetPalette;
	re.BeginFrame = DX12_BeginFrame;
	re.EndFrame = DX12_EndFrame;

	re.AppActivate = DX12_AppActivate;


	return re;
}