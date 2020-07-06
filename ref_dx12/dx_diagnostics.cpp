#include "dx_diagnostics.h"

#include <stdarg.h>

void Diagnostics::BeginEvent(ID3D12GraphicsCommandList* commandList, std::string_view msg)
{
	// I looked up implementation of this method in pix_win.h, and from my understanding
	// first argument indicates how string should be interpreted. 0 - unicode, 1 - ASCII 
	commandList->BeginEvent(1, msg.data(), msg.size() * sizeof(char));
}

void Diagnostics::BeginEventf(ID3D12GraphicsCommandList* commandList, const char* fmt, ...)
{
	char buffer[0x1000];

	va_list argptr;

	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);

	BeginEvent(commandList, buffer);
}

void Diagnostics::EndEvent(ID3D12GraphicsCommandList* commandList)
{
	commandList->EndEvent();
}

