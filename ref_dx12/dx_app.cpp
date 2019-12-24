#include "dx_app.h"

#include <cassert>
#include <string>


#include "../win32/winquake.h"
#include "dx_utils.h"

DXApp& DXApp::Inst()
{
	static DXApp* app = nullptr;

	if (app == nullptr)
	{
		app = new DXApp();
	}

	return *app;
}

void DXApp::Init(WNDPROC WindowProc, HINSTANCE hInstance)
{
	InitWin32(WindowProc, hInstance);
}

void DXApp::InitWin32(WNDPROC WindowProc, HINSTANCE hInstance)
{
	//#DEBUG IMPORTANT TO DESTROY OLD WINDOWS if it exists
	const std::wstring WindowsClassName = DXUtils::StringToWString("Quake 2");

	char modeVarName[] = "gl_mode";
	char modeVarVal[] = "3";
	cvar_t* mode = GetRefImport().Cvar_Get(modeVarName, modeVarVal, CVAR_ARCHIVE);
	
	assert(mode);

	int Width = 0;
	int Height = 0;
	GetRefImport().Vid_GetModeInfo(&Width, &Height, static_cast<int>(mode->value));

	WNDCLASS WindowClass;
	RECT	 ScreenRect;

	WindowClass.style			= 0;
	WindowClass.lpfnWndProc	= WindowProc;
	WindowClass.cbClsExtra		= 0;
	WindowClass.cbWndExtra		= 0;
	WindowClass.hInstance		= hInstance;
	WindowClass.hIcon			= 0;
	WindowClass.hCursor		= LoadCursor(NULL, IDC_ARROW);
	WindowClass.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_GRAYTEXT);
	WindowClass.lpszMenuName	= 0;
	WindowClass.lpszClassName  = static_cast<LPCWSTR>(WindowsClassName.c_str());

	assert(RegisterClass(&WindowClass) && "Failed to register win class.");

	ScreenRect.left = 0;
	ScreenRect.top = 0;
	ScreenRect.right = Width;
	ScreenRect.bottom = Height;

	// For now always run in windows mode (not fullscreen)
	const int ExStyleBist = 0;
	const int StyleBits = WINDOW_STYLE;

	AdjustWindowRect(&ScreenRect, StyleBits, FALSE);

	const int AdjustedWidth = ScreenRect.right - ScreenRect.left;
	const int AdjustedHeight = ScreenRect.bottom - ScreenRect.top;

	char xPosVarName[] = "vid_xpos";
	char xPosVarVal[] = "0";
	cvar_t* vid_xpos = GetRefImport().Cvar_Get(xPosVarName, xPosVarVal, 0);
	char yPosVarName[] = "vid_ypos";
	char yPosVarVal[] = "0";
	cvar_t* vid_ypos = GetRefImport().Cvar_Get(yPosVarName, yPosVarVal, 0);
	const int x = vid_xpos->value;
	const int y = vid_ypos->value;

	 m_hWindows = CreateWindowEx(
		ExStyleBist,
		WindowsClassName.c_str(),
		WindowsClassName.c_str(),
		StyleBits,
		x, y, AdjustedWidth, AdjustedHeight,
		NULL,
		NULL,
		hInstance,
		NULL
	);

	assert(m_hWindows && "Failed to create windows");

	ShowWindow(m_hWindows, SW_SHOW);
	UpdateWindow(m_hWindows);

	SetForegroundWindow(m_hWindows);
	SetFocus(m_hWindows);

	GetRefImport().Vid_NewWindow(Width, Height);
}
