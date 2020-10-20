#pragma once

#include <mutex>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <memory>
#include <atomic>

#include "dx_common.h"

template<typename T>
struct LockObject
{
	std::mutex mutex;
	T obj;
};

template<typename T>
using LockVector_t = LockObject<std::vector<T>>;

template<typename kT, typename vT>
using LockUnorderedMap_t = LockObject<std::unordered_map<kT, vT>>;

#define DO_IN_LOCK(object, action)  \
	object.mutex.lock();			\
	object.obj.action;				\
	object.mutex.unlock()
	
#define ASSERT_MAIN_THREAD ThreadingUtils::AssertMainThread()

class Semaphore
{
public:
	Semaphore(int waitForValue);

	Semaphore(const Semaphore&) = delete;
	Semaphore& operator=(const Semaphore&) = delete;

	Semaphore(Semaphore&& other) = delete;
	Semaphore& operator=(Semaphore&& other) = delete;

	~Semaphore();

	void Signal();
	void Wait() const;

	static void WaitForMultipleAny(const std::vector<std::shared_ptr<Semaphore>> waitForSemaphores);

private:

	const int waitValue = 0;

	std::atomic<int> counter = 0;
	HANDLE winSemaphore = NULL;

};

namespace ThreadingUtils
{
	void Init();

	std::thread::id GetMainThreadId();

	void AssertMainThread();

	template<typename T>
	void AssertUnlocked(LockObject<T>& obj)
	{
#ifdef _DEBUG
		// Note according to documentation of try_lock() "This function is allowed to fail spuriously". 
		assert(obj.mutex.try_lock() == true && "Oops. This mutex should be unlocked at this point.");
		obj.mutex.unlock();
#endif
	};

}