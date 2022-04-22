#include "dx_diagnostics.h"

#include <stdarg.h>

#include "dx_utils.h"

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


void Diagnostics::SetResourceName(ID3D12Object* resource, const std::string& name)
{
	if constexpr (ENABLE_DX_RESOURCE_NAMING == true)
	{
		assert(resource != nullptr && "SetResourceNameReceived invalid resource");

		resource->SetName(Utils::StringToWideString(name).c_str());
	}
}

void Diagnostics::SetResourceNameWithAutoId(ID3D12Object* resource, const std::string& name)
{
	if constexpr (ENABLE_DX_RESOURCE_NAMING == true)
	{
		static std::atomic<uint64_t> uniqueId = 0;

		SetResourceName(resource, name + std::to_string(uniqueId.fetch_add(1)));
	}
}

namespace Logs
{
	std::array<Event, BUFFER_SIZE> gEventsBuffer;
	std::atomic<int> gPos = 0;

	constexpr bool gEnableLogs = true;
	constexpr bool gPrintToConsole = true;

	constexpr bool CategoryEnabled[static_cast<int>(Category::_Count)] =
	{
		true,	// Generic,
		false,	// Synchronization,
		false,	// FrameSubmission,
		false,	// Textures, 
		false,	// Job
		false,	// Parser
	};
}


void Logs::Logf(Category category, const char* fmt, ...)
{
	if constexpr (gEnableLogs == true)
	{
		char buffer[1024];

		va_list argptr;

		va_start(argptr, fmt);
		vsprintf(buffer, fmt, argptr);
		va_end(argptr);

		Log(category, buffer);
	}
}

void Logs::Log(Category category, std::string_view message)
{
	if constexpr (gEnableLogs == true)
	{
		if (CategoryEnabled[static_cast<int>(category)] == true)
		{
			int index = gPos.fetch_add(1);
			index = index & (BUFFER_SIZE - 1); // Wrap the buffer size

			Event& event = gEventsBuffer[index];

			event.category = category;
			event.message = message;

			event.threadId = std::this_thread::get_id();

			if constexpr (gPrintToConsole == true)
			{
				Utils::VSCon_Printf("LOG %s: %s \n", CategoryToString(category), event.message.c_str());
			}
		}
	}
}
