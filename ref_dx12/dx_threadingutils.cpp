#include "dx_threadingutils.h"

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

	// Remember, fetch_add() will return old value, that's why -1 
	if (counter.fetch_add(1) >= waitValue - 1)
	{
		ReleaseSemaphore(winSemaphore, 1, NULL);
	};
}

void Semaphore::Wait() const
{
	if (counter.load() < waitValue)
	{
		DWORD res = WaitForSingleObject(winSemaphore, INFINITE);

		assert(res == WAIT_OBJECT_0 && "Semaphore wait ended in unexpected way.");
	}
}

void Semaphore::WaitForMultipleAny(const std::vector<std::shared_ptr<Semaphore>> waitForSemaphores)
{
	assert(waitForSemaphores.empty() == false && "WaitForMultipleAny received empty semaphore list.");

	std::vector<HANDLE> winSemaphores;
	winSemaphores.reserve(waitForSemaphores.size());

	for (const std::shared_ptr<Semaphore>& s : waitForSemaphores)
	{
		assert(s != nullptr && "WaitForMultipleAny received empty pointer");

		// If any of semaphores is ready, we are done
		if (s->counter.load() >= s->waitValue)
		{
			return;
		}

		winSemaphores.push_back(s->winSemaphore);
	}

	DWORD res = WaitForMultipleObjects(winSemaphores.size(), winSemaphores.data(), FALSE, INFINITE);
	assert(res != WAIT_TIMEOUT && res != WAIT_FAILED && "WaitForMultipleAny ended in unexpected way.");
}
