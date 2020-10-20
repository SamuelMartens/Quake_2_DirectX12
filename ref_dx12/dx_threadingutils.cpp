#include "dx_threadingutils.h"

#include "dx_diagnostics.h"

#ifdef _DEBUG
	#define SEMAPHORE_TIME_OUT 10000
#else
	#define SEMAPHORE_TIME_OUT INFINITE
#endif

namespace
{
	std::thread::id gMainThreadId;
}

Semaphore::Semaphore(int waitForValue) :
	waitValue(waitForValue)
{
	winSemaphore = CreateSemaphore(NULL, 0, 1, NULL);
}

Semaphore::~Semaphore()
{
	CloseHandle(winSemaphore);
}

void Semaphore::Signal()
{
	assert(waitValue != 0 && "Not initialized semaphore is signaled");

	Logs::Logf(Logs::Category::Synchronization, "Semaphore signaled, handle %x", reinterpret_cast<unsigned>(winSemaphore));

	// Remember, fetch_add() will return old value, that's why -1 
	if (counter.fetch_add(1) >= waitValue - 1)
	{
		ReleaseSemaphore(winSemaphore, 1, NULL);

		Logs::Logf(Logs::Category::Synchronization, "Semaphore released, handle %x", reinterpret_cast<unsigned>(winSemaphore));
	};
}

void Semaphore::Wait() const
{
	Logs::Logf(Logs::Category::Synchronization, "Semaphore wait entered, handle %x", reinterpret_cast<unsigned>(winSemaphore));

	if (counter.load() < waitValue)
	{
		LONG prevVal = -1;
		DWORD res = WaitForSingleObject(winSemaphore, SEMAPHORE_TIME_OUT);
		// Restore signaled state, since Wait() will change semaphore value
		ReleaseSemaphore(winSemaphore, 1, &prevVal);
		
		assert(res == WAIT_OBJECT_0 && "Semaphore wait ended in unexpected way.");
		assert(prevVal == 0 && "Prev val assert");
	}

	Logs::Logf(Logs::Category::Synchronization, "Semaphore wait finished, handle %x", reinterpret_cast<unsigned>(winSemaphore));
}

void Semaphore::WaitForMultipleAny(const std::vector<std::shared_ptr<Semaphore>> waitForSemaphores)
{
	Logs::Logf(Logs::Category::Synchronization, "Semaphore Multi wait entered");

	assert(waitForSemaphores.empty() == false && "WaitForMultipleAny received empty semaphore list.");

	std::vector<HANDLE> winSemaphores;
	winSemaphores.reserve(waitForSemaphores.size());

	for (const std::shared_ptr<Semaphore>& s : waitForSemaphores)
	{
		assert(s != nullptr && "WaitForMultipleAny received empty pointer");

		// If any of semaphores is ready, we are done
		if (s->counter.load() >= s->waitValue)
		{
			Logs::Logf(Logs::Category::Synchronization, "Semaphore multi wait finished");

			return;
		}

		winSemaphores.push_back(s->winSemaphore);
	}
	LONG prevVal = -1;
	DWORD res = WaitForMultipleObjects(winSemaphores.size(), winSemaphores.data(), FALSE, SEMAPHORE_TIME_OUT);
	assert(res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + waitForSemaphores.size()
		&& "WaitForMultipleAny ended in unexpected way.");
	
	// Restore signaled state, since Wait() will change semaphore value
	ReleaseSemaphore(winSemaphores[res - WAIT_OBJECT_0], 1, &prevVal);
	assert(prevVal == 0 && "Multiple prev val is not equal to 0");

	Logs::Logf(Logs::Category::Synchronization, "Semaphore Multi wait finished, handle %x", reinterpret_cast<unsigned>(winSemaphores[res - WAIT_OBJECT_0]));

}

void ThreadingUtils::Init()
{
	gMainThreadId = std::this_thread::get_id();
}

std::thread::id ThreadingUtils::GetMainThreadId()
{
	return gMainThreadId;
}

void ThreadingUtils::AssertMainThread()
{
	assert(std::this_thread::get_id() == gMainThreadId && "This supposed to be executed in main thread only.");
}
