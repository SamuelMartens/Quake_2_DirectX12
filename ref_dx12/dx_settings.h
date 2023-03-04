#pragma once

#include <dxgi.h>
#include <string>
#include <d3dcompiler.h>
#include <d3d12.h>

#include "dx_assert.h"

namespace Settings
{
	constexpr int		 FRAMES_NUM = 3;
	constexpr int		 SWAP_CHAIN_BUFFER_COUNT = FRAMES_NUM;

	constexpr bool		 MSAA_ENABLED = false;
	constexpr int		 MSAA_SAMPLE_COUNT = 4;

	constexpr int		 TRANSPARENT_TABLE_VAL = 255;
	constexpr int		 CONST_BUFFER_ALIGNMENT = 256;

	constexpr int		 FRAME_STREAMING_CBV_SRV_DESCRIPTOR_HEAP_SIZE = 16 * 1024;
	constexpr int		 CBV_SRV_DESCRIPTOR_HEAP_SIZE = 32 * 1024;
	constexpr int		 RTV_DTV_DESCRIPTOR_HEAP_SIZE = 64;
	constexpr int		 SAMPLER_DESCRIPTOR_HEAP_SIZE = 16;

	constexpr int		 GPU_RESOURCE_DELETION_THRESHOLD = 16;

	constexpr int		 COMMAND_LISTS_PER_FRAME = 21;
	// Try to avoid to set up any particular number for this, instead change command lists per frame
	constexpr int		 COMMAND_LISTS_NUM = COMMAND_LISTS_PER_FRAME * FRAMES_NUM;

	// 128 MB of upload memory
	constexpr int		 UPLOAD_MEMORY_BUFFER_SIZE = 128 * 1024 * 1024;
	constexpr int		 UPLOAD_MEMORY_BUFFER_HANDLERS_NUM = 64 * 1024;
	// 256 MB of default memory
	constexpr int		 DEFAULT_MEMORY_BUFFER_SIZE = 256 * 1024 * 1024;
	constexpr int		 DEFAULT_MEMORY_BUFFER_HANDLERS_NUM = 64 * 1024;

	constexpr bool		 DEBUG_LAYER_ENABLED = true;
	constexpr bool		 DEBUG_MESSAGE_FILTER_ENABLED = true;

	constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
	constexpr DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;

	/* Draw settings */
	constexpr int		 CHAR_SIZE = 8;


	/* Grammar Data */
	extern const std::string	GRAMMAR_DIR;

	extern const std::string	GRAMMAR_PASS_FILENAME;
	extern const std::string	GRAMMAR_FRAMEGRAPH_FILENAME;
	extern const std::string	GRAMMAR_LIGHT_BAKING_RESULT_FILENAME;

	/* Frame Graph Builder  */
	extern const std::string	FRAMEGRAPH_DIR;
	extern const std::string	FRAMEGRAPH_PASS_FILE_EXT;
	extern const std::string	FRAMEGRAPH_FILE_EXT;

	/* Data */

	extern const std::string	DATA_DIR;
	extern const std::string	LIGHT_BAKING_DATA_FILENAME;

	//constexpr UINT		SHADER_COMPILATION_FLAGS = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	constexpr UINT		SHADER_COMPILATION_FLAGS = 0;

	extern const std::string SHADER_FEATURE_LEVEL;

	/* Debug GUI stuff */
	constexpr bool DEBUG_GUI_ENABLED = true;


	/* Light Baker */
	constexpr float AREA_LIGHTS_MIN_DISTANCE = 1.0f;
	constexpr float AREA_LIGHTS_MAX_DISTANCE = 1000.0f;
	constexpr float POINT_LIGHTS_MAX_DISTANCE = 1000.0f;

	constexpr float RUSSIAN_ROULETTE_ABSORBTION_PROBABILITY = 0.5f;
	
	constexpr int PROBE_SAMPLES_NUM = 1024;

	constexpr int GUARANTEED_BOUNCES_NUM = 2;
	constexpr int AREA_LIGHTS_SAMPLES_NUM = 4;

	constexpr float CLUSTER_PROBE_GRID_INTERVAL = 50.0f;

	// Because of floating point math errors, funding and reconstruction intersection point
	// might not work correctly sometimes. This small Epsilon is used to fix this problem
	constexpr float PATH_TRACING_EPSILON =  0.00015f;

	/* Start up options */
	constexpr bool LOAD_LIGHT_BAKING_DATA_ON_START_UP = true;

	/* Clustered lighting */
	constexpr int CLUSTERED_LIGHT_LIST_SIZE = 512;
}