#pragma once

#include <mutex>
#include <vector>
#include <unordered_map>
#include <cassert>

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
	

template<typename T>
void AssertUnlocked(LockObject<T>& obj) 
{
#ifdef _DEBUG
	//#DEBUG "This function is allowed to fail spuriously" Damn shall, I use this?
	assert(obj.mutex.try_lock() == true && "Oops. This mutex should be unlocked at this point.");
	obj.mutex.unlock();
#endif
};