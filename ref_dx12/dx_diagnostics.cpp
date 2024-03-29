#include "dx_diagnostics.h"

#include "dx_utils.h"
#include "dx_assert.h"

void Diagnostics::BeginEvent(ID3D12GraphicsCommandList* commandList, std::string_view msg)
{
	// I looked up implementation of this method in pix_win.h, and from my understanding
	// first argument indicates how string should be interpreted. 0 - unicode, 1 - ASCII 
	commandList->BeginEvent(1, msg.data(), msg.size() * sizeof(char));
}

void Diagnostics::EndEvent(ID3D12GraphicsCommandList* commandList)
{
	commandList->EndEvent();
}

void Diagnostics::SetResourceName(ID3D12Object* resource, const std::string& name)
{
	if constexpr (ENABLE_DX_RESOURCE_NAMING == true)
	{
		DX_ASSERT(resource != nullptr && "SetResourceNameReceived invalid resource");

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
	std::array<Event, BUFFER_SIZE> EventsBuffer;
	std::atomic<int> BufferPos = 0;

	// NOTE: Logs are very slow if printed to console
	constexpr bool PRINT_TO_CONSOLE = true;

	constexpr bool CategoryEnabled[static_cast<int>(Category::_Count)] =
	{
		false,	// Generic,
		false,	// Synchronization,
		false,	// FrameSubmission,
		false,	// Resource, 
		false,	// Job
		true,	// Parser
		true,	// Validation
	};
}

void Logs::Log(Category category, const std::string& message)
{
	if constexpr (ENABLE_LOGS == true)
	{
		if (CategoryEnabled[static_cast<int>(category)] == true)
		{
			int index = BufferPos.fetch_add(1);
			index = index & (BUFFER_SIZE - 1); // Wrap the buffer size

			Event& event = EventsBuffer[index];

			event.category = category;
			event.message = message;

			event.threadId = std::this_thread::get_id();

			if constexpr (PRINT_TO_CONSOLE == true)
			{
				const std::string msg = std::format("LOG {}: {} \n", CategoryToString(category), event.message);
				Utils::VSCon_Print(msg);
			}
		}
	}
}
