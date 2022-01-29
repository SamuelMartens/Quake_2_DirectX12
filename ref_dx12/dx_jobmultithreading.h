#pragma once

#include <functional>
#include <cassert>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>

#include "dx_utils.h"

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

class JobSystem
{
public:
	
	DEFINE_SINGLETON(JobSystem);

	void Init();

	JobQueue& GetJobQueue();
	int GetWorkerThreadsNum() const;

private:

	JobQueue jobQueue;

	std::vector<WorkerThread> workerThreads;
};
