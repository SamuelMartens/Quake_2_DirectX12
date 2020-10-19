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


namespace Logs
{
	std::array<Event, BUFFER_SIZE> eventsBuffer;
	std::atomic<int> pos = 0;

	constexpr bool enableLogs = false;
	constexpr bool printToConsole = true;

	constexpr bool CategoryEnabled[static_cast<int>(Category::_Count)] =
	{
		true, // Generic,
		false, // Synchronization,
		false, // FrameSubmission,
		true,  // Textures, 
		false   // Job
	};
}


void Logs::Logf(Category category, const char* fmt, ...)
{
	if constexpr (enableLogs == true)
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
	if constexpr (enableLogs == true)
	{
		if (CategoryEnabled[static_cast<int>(category)] == true)
		{
			int index = pos.fetch_add(1);
			index = index & (BUFFER_SIZE - 1); // Wrap the buffer size

			Event& event = eventsBuffer[index];

			event.category = category;
			event.message = message;

			event.threadId = std::this_thread::get_id();

			if constexpr (printToConsole == true)
			{
				Utils::VSCon_Printf("LOG %s: %s \n", CategoryToString(category), event.message.c_str());
			}
		}
	}
}
