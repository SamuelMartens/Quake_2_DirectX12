#pragma once

#include <mutex>
#include <vector>
#include <unordered_map>
#include <cassert>
#include <memory>
#include <atomic>

#include "dx_common.h"
#include "dx_utils.h"
#include "dx_texture.h"

class Frame;
class CommandList;

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
	static void WaitForMultipleAll(const std::vector<std::shared_ptr<Semaphore>> waitForSemaphores);

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

class GPUJobContext
{
public:

	/* 
		SHOULD BE SAFE TO COPY

		Watch this to be lightweight
	*/
	GPUJobContext(Frame& frameVal, CommandList* commandListVal = nullptr);

	void CreateDependencyFrom(std::vector<GPUJobContext*> dependsFromList);
	void CreateDependencyFrom(std::vector<GPUJobContext>& dependsFromList);

	void SignalDependencies();
	void WaitDependency() const;



	Frame& frame;
	CommandList* commandList = nullptr;

	std::vector<ResourceProxy> internalTextureProxies;

private:

	// This properties represent relationship between jobs that Semaphore implements.
	// Which is one to many, Which means one job can wait for multiple jobs to be finished,
	// each one will increment semaphore by one.
	std::vector<std::shared_ptr<Semaphore>> signalDependencies;
	std::shared_ptr<Semaphore> waitDependancy;

};


// Order is extremely important. Consider construction/destruction order
// when adding something
#define JOB_GUARD( context ) \
	DependenciesRAIIGuard_t dependenciesGuard(context); \
	CommandListRAIIGuard_t commandListGuard(*context.commandList)

using DependenciesRAIIGuard_t = Utils::RAIIGuard<GPUJobContext,
	nullptr, &GPUJobContext::SignalDependencies>;