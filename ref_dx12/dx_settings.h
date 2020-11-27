#pragma once

#include <dxgi.h>
#include <string>

namespace Settings
{
	constexpr static int		 FRAMES_NUM = 3;
	constexpr static int		 SWAP_CHAIN_BUFFER_COUNT = FRAMES_NUM;
	constexpr static bool		 MSAA_ENABLED = false;
	constexpr static int		 MSAA_SAMPLE_COUNT = 4;
	constexpr static int		 TRANSPARENT_TABLE_VAL = 255;
	constexpr static int		 CONST_BUFFER_ALIGNMENT = 256;
	constexpr static int		 DYNAM_OBJECT_CONST_BUFFER_POOL_SIZE = 512;

	constexpr static int		 CBV_SRV_DESCRIPTOR_HEAP_SIZE = 512;
	constexpr static int		 RTV_DTV_DESCRIPTOR_HEAP_SIZE = FRAMES_NUM;

	constexpr static int		 COMMAND_LISTS_PER_FRAME = 7;
	// Try to avoid to set up any particular number for this, instead change command lists per frame
	constexpr static int		 COMMAND_LISTS_NUM = COMMAND_LISTS_PER_FRAME * FRAMES_NUM;

	// 128 MB of upload memory
	constexpr static int		 UPLOAD_MEMORY_BUFFER_SIZE = 128 * 1024 * 1024;
	constexpr static int		 UPLOAD_MEMORY_BUFFER_HANDLERS_NUM = 16382 * 2 * 2;
	// 256 MB of default memory
	constexpr static int		 DEFAULT_MEMORY_BUFFER_SIZE = 256 * 1024 * 1024;
	constexpr static int		 DEFAULT_MEMORY_BUFFER_HANDLERS_NUM = 16382;

	constexpr static bool		 DEBUG_LAYER_ENABLED = false;
	constexpr static bool		 DEBUG_MESSAGE_FILTER_ENABLED = true;

	constexpr static DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr static DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;

	/* Material Compiler  */

	extern const std::string	 GRAMMAR_DIR;
	extern const std::string	 GRAMMAR_PASS_FILENAME;
	extern const std::string	 GRAMMAR_MATERIAL_FILENAME;

	extern const std::string	 MATERIAL_DIR;
	extern const std::string	 MATERIAL_PASS_FILE_EXT;
	extern const std::string	 MATERIAL_FILE_EXT;
}