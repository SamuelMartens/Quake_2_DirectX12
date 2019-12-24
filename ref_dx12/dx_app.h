#pragma once

#include <windows.h>

extern "C"
{
	#include "../client/ref.h"
};

class DXApp
{
private:
	DXApp() = default;

public:

	DXApp(const DXApp&) = delete;
	DXApp& operator=(const DXApp&) = delete;
	DXApp(DXApp&&) = delete;
	DXApp& operator=(DXApp&&) = delete;

	~DXApp() = default;

	static DXApp& Inst();

 	void Init(WNDPROC WindowProc, HINSTANCE hInstance);

	const refimport_t& GetRefImport() const { return m_RefImport; };
	void SetRefImport(refimport_t RefImport) { m_RefImport = RefImport; };

private:

	refimport_t m_RefImport;


	/* Initialize win32 specific stuff */
	void InitWin32(WNDPROC WindowProc, HINSTANCE hInstance);

	HWND		m_hWindows;
};