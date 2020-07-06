#pragma once

#include <string_view>
#include <d3d12.h>

namespace Diagnostics
{
	/*
		PIX doesn't work for x32 applications so does its event library. So here I am
		directly using Command List event methods.
	*/
	void BeginEvent(ID3D12GraphicsCommandList* commandList, std::string_view msg);
	void BeginEventf(ID3D12GraphicsCommandList* commandList, const char* fmt, ...);

	void EndEvent(ID3D12GraphicsCommandList* commandList);

}