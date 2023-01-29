#pragma once

#include <string_view>
#include <d3d12.h>
#include <thread>
#include <string>
#include <array>
#include <atomic>
#include <string_view>
#include <format>

#include "dx_assert.h"

namespace Diagnostics
{
#ifdef ENABLE_VALIDATION
	constexpr bool ENABLE_DX_RESOURCE_NAMING = true;
#else
	constexpr bool ENABLE_DX_RESOURCE_NAMING = false;
#endif // ENABLE_VALIDATION

	/*
		PIX doesn't work for x32 applications so does its event library. So here I am
		directly using Command List event methods.

		NOTE: Event markers can only be applied to open command lists.
	*/
	void BeginEvent(ID3D12GraphicsCommandList* commandList, std::string_view msg);
	void EndEvent(ID3D12GraphicsCommandList* commandList);

	void SetResourceName(ID3D12Object* resource, const std::string& name);
	void SetResourceNameWithAutoId(ID3D12Object* resource, const std::string& name);
}

// Idea from https://preshing.com/20120522/lightweight-in-memory-logging/
namespace Logs
{
	// If this one is changed, change CategoryEnabled and CategoryStrings
	enum class Category
	{
		Generic,
		Synchronization,
		FrameSubmission,
		Resource,
		Job,
		Parser,

		_Count
	};

	constexpr char CategoryString[static_cast<int>(Category::_Count)][25] =
	{
		"Generic",
		"Synchronization",
		"FrameSubmission",
		"Resource",
		"Job",
		"Parser"
	};

	struct Event
	{
		std::thread::id threadId;
		std::string message;
		Category category = Category::Generic;
	};

	constexpr int BUFFER_SIZE = 4098;
	constexpr bool ENABLE_LOGS = true;

	extern std::array<Event, BUFFER_SIZE> EventsBuffer;
	extern  std::atomic<int> BufferPos;

	constexpr const char* CategoryToString(Category category)
	{
		return CategoryString[static_cast<int>(category)];
	};

	void Log(Category category, const std::string& message);

	template <typename... Args>
	void Logf(Category category, const std::string& fmt, Args&&... args)
	{
		if constexpr (ENABLE_LOGS == true)
		{
			Log(category, std::vformat(fmt, std::make_format_args(args...)));
		}
	}
}