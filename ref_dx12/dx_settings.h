#pragma once

#include <dxgi.h>
#include <string>
#include <d3dcompiler.h>

namespace Settings
{
	constexpr int		 FRAMES_NUM = 3;
	constexpr int		 SWAP_CHAIN_BUFFER_COUNT = FRAMES_NUM;
	constexpr bool		 MSAA_ENABLED = false;
	constexpr int		 MSAA_SAMPLE_COUNT = 4;
	constexpr int		 TRANSPARENT_TABLE_VAL = 255;
	constexpr int		 CONST_BUFFER_ALIGNMENT = 256;
	constexpr int		 DYNAM_OBJECT_CONST_BUFFER_POOL_SIZE = 512;

	constexpr int		 CBV_SRV_DESCRIPTOR_HEAP_SIZE = 1024;
	constexpr int		 RTV_DTV_DESCRIPTOR_HEAP_SIZE = FRAMES_NUM;
	constexpr int		 SAMPLER_DESCRIPTOR_HEAP_SIZE = 16;

	constexpr int		 COMMAND_LISTS_PER_FRAME = 7;
	// Try to avoid to set up any particular number for this, instead change command lists per frame
	constexpr int		 COMMAND_LISTS_NUM = COMMAND_LISTS_PER_FRAME * FRAMES_NUM;

	// 128 MB of upload memory
	constexpr int		 UPLOAD_MEMORY_BUFFER_SIZE = 128 * 1024 * 1024;
	constexpr int		 UPLOAD_MEMORY_BUFFER_HANDLERS_NUM = 16382 * 2 * 2;
	// 256 MB of default memory
	constexpr int		 DEFAULT_MEMORY_BUFFER_SIZE = 256 * 1024 * 1024;
	constexpr int		 DEFAULT_MEMORY_BUFFER_HANDLERS_NUM = 16382;

	constexpr bool		 DEBUG_LAYER_ENABLED = false;
	constexpr bool		 DEBUG_MESSAGE_FILTER_ENABLED = true;

	constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;

	/* Material Compiler  */

	extern const std::string	 GRAMMAR_DIR;
	extern const std::string	 GRAMMAR_PASS_FILENAME;
	extern const std::string	 GRAMMAR_FRAMEGRAPH_FILENAME;

	extern const std::string	 FRAMEGRAPH_DIR;
	extern const std::string	 FRAMEGRAPH_PASS_FILE_EXT;
	extern const std::string	 FRAMEGRAPH_FILE_EXT;

#ifdef _DEBUG
	constexpr UINT		SHADER_COMPILATION_FLAGS = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	constexpr UINT		SHADER_COMPILATION_FLAGS = 0;
#endif
}