#pragma once

#include <string_view>
#include <d3d12.h>
#include <thread>
#include <string>
#include <array>
#include <atomic>
#include <string_view>

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

// Idea from https://preshing.com/20120522/lightweight-in-memory-logging/
namespace Logs
{
	// If this one is changed, change CategoryEnabled and CategoryStrings
	enum class Category
	{
		Generic,
		Synchronization,
		FrameSubmission,
		Textures,
		Job,

		_Count
	};

	constexpr char CategoryString[static_cast<int>(Category::_Count)][25] =
	{
		"Generic",
		"Synchronization",
		"FrameSubmission",
		"Textures",
		"Job"
	};

	struct Event
	{
		std::thread::id threadId;
		std::string message;
		Category category = Category::Generic;
	};

	static constexpr int BUFFER_SIZE = 65536;
	extern std::array<Event, BUFFER_SIZE> eventsBuffer;
	extern  std::atomic<int> pos;

	constexpr const char* CategoryToString(Category category)
	{
		return CategoryString[static_cast<int>(category)];
	};

	void Logf(Category category, const char* fmt, ...);
	void Log(Category category, std::string_view message);
}