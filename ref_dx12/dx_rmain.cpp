
#include "dx_app.h"

extern "C"
{
	#include "../client/ref.h"
}

#include "dx_local.h"



/** Wrap C++ functionality in C interface **/
// Register map as the world
void DX12_BeginRegistration(char* map)
{
	Renderer::Inst().StartLevelLoading(map);
}

struct model_s* DX12_RegModel(char* model)
{
	return Renderer::Inst().RegisterModel(model);
}

struct image_s* DX12_RegSkin(char* skin)
{
	return NULL;
}

struct image_s* DX12_Draw_FindPic(char* name)
{
	return reinterpret_cast<image_s*>(Renderer::Inst().RegisterDrawPic(name));
}

void DX12_SetSky (char* name, float rotate, vec3_t axis)
{}

void DX12_EndReg(void)
{
	Renderer::Inst().EndLevelLoading();
}

void DX12_RenderFrame(refdef_t *fd)
{
	Renderer::Inst().RenderFrameAsync(*fd);
}

void DX12_Draw_GetPicSize(int *w, int *h, char *name)
{
	Renderer::Inst().GetDrawTextureSize(w, h, name);
}

void DX12_Draw_Pic(int x, int y, char *name)
{
	Renderer::Inst().AddDrawCall_Pic(x, y, name);
}

void DX12_Draw_StretchPic(int x, int y, int w, int h, char *name)
{}

void DX12_Draw_Char(int x, int y, int c)
{
	Renderer::Inst().AddDrawCall_Char(x, y, c);
}

void DX12_Draw_TileClear(int x, int y, int w, int h, char *name)
{}

void DX12_Draw_Fill(int x, int y, int w, int h, int c)
{}

void DX12_Draw_FadeScreen(void)
{}

void DX12_Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, byte *data)
{
	Renderer::Inst().AddDrawCall_RawPic(x, y, w, h, cols, rows, reinterpret_cast<std::byte*>(data));
}

qboolean DX12_Init( void *hinstance, void *hWnd )
{
	Renderer::Inst().Init(
		reinterpret_cast<WNDPROC>(hWnd),
		reinterpret_cast<HINSTANCE>(hinstance)
	);

	return qTrue;
}

void DX12_Shutdown(void)
{}

void DX12_BeginFrame( float camera_separation )
{
	Renderer::Inst().BeginFrameAsync();
}

void DX12_EndFrame(void)
{
	Renderer::Inst().EndFrameAsync();
}

void DX12_SetPalette(const unsigned char *palette)
{
	Renderer::Inst().SetPalette(palette);
}

void DX12_AppActivate( qboolean active )
{}

refexport_t GetRefAPI (refimport_t rimp)
{
	Renderer::Inst().SetRefImport(rimp);
	refexport_t re;

	re.api_version = API_VERSION;

	re.BeginRegistration = DX12_BeginRegistration;
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

	Swap_Init();

	return re;
}