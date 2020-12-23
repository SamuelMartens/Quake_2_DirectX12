#pragma once

#include <functional>
#include <cassert>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>

#include "dx_frame.h"
#include "dx_commandlist.h"
#include "dx_threadingutils.h"

// Core job stuff

class Job
{
public:

	explicit Job(std::function<void()> jobCallback);

	Job(const Job&) = delete;
	Job& operator=(const Job&) = delete;

	Job(Job&&) = default;
	Job& operator=(Job&&) = default;

	~Job() = default;

	void Execute();

private:
	
	std::function<void()> callback;

};

class JobQueue
{
public:

	JobQueue() = default;

	JobQueue(const JobQueue&) = delete;
	JobQueue& operator=(const JobQueue&) = delete;

	JobQueue(JobQueue&&) = delete;
	JobQueue& operator=(JobQueue&&) = delete;

	~JobQueue() = default;

	void Enqueue(Job job);

	Job Dequeue();

private:

	std::mutex mutex;
	std::queue<Job> queue;

	std::condition_variable conditionalVariable;
};

class WorkerThread
{
public:

	WorkerThread(std::function<void()> callback);

	WorkerThread(const WorkerThread&) = delete;
	WorkerThread& operator=(const WorkerThread&) = delete;

	WorkerThread(WorkerThread&&) = default;
	WorkerThread& operator=(WorkerThread&&) = default;

	~WorkerThread() = default;

private:

	std::thread thread;
};

//#TODO make this singleton
class JobSystem
{
public:

	JobSystem() = default;

	JobSystem(const JobSystem&) = delete;
	JobSystem& operator=(const JobSystem&) = delete;

	JobSystem(JobSystem&&) = delete;
	JobSystem& operator=(JobSystem&&) = delete;

	~JobSystem() = default;

	void Init();

	JobQueue& GetJobQueue();

private:

	JobQueue jobQueue;

	std::vector<WorkerThread> workerThreads;
};



// Utilities 
struct Context
{
	/* SHOULD BE SAFE TO COPY */
	Context(Frame& frameVal, CommandList& commandListVal);

	void CreateDependencyFrom(std::vector<Context*> dependsFromList);
	void CreateDependencyFrom(std::vector<Context>& dependsFromList);
	void SignalDependencies();

	// This properties represent relationship between jobs that Semaphore implements.
	// Which is one to many, Which means one job can wait for multiple jobs to be finished,
	// each one will increment semaphore by one.
	std::vector<std::shared_ptr<Semaphore>> signalDependencies;
	std::shared_ptr<Semaphore> waitDependancy;

	Frame& frame;
	CommandList& commandList;
};

using DependenciesRAIIGuard_t = Utils::RAIIGuard<Context, 
	nullptr, &Context::SignalDependencies>;