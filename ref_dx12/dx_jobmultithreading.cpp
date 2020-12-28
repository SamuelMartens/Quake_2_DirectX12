#include "dx_jobmultithreading.h"

Job::Job(std::function<void()> jobCallback):
	callback(jobCallback)
{}

void Job::Execute()
{
	assert(callback && "Trying to execute job with empty callback");

	callback();
}

void JobQueue::Enqueue(Job job)
{
	std::scoped_lock<std::mutex> lockGuard(mutex);

	queue.push(std::move(job));

	conditionalVariable.notify_one();
}

Job JobQueue::Dequeue()
{
	std::unique_lock<std::mutex> uniqueLock(mutex);

	while (queue.empty())
	{
		conditionalVariable.wait(uniqueLock);
	}

	Job result = std::move(queue.front());

	queue.pop();

	return result;
}

WorkerThread::WorkerThread(std::function<void()> callback)
	:thread(callback)
{
	// Let this work forever
	thread.detach();
}




void JobSystem::Init()
{
	// Minus one, because we also have main thread
	const int workerThreadsNum = std::thread::hardware_concurrency() - 1;

	// --- WORKER THREAD CALLBACK ---
	std::function<void()> workerThreadCallback = [this]()
	{
		// Not gonna set thread affinity for now. Win API documentation recommends to
		// leave it as it is, so without precise advice I am gonna follow that advice

		while (true)
		{
			Job job = GetJobQueue().Dequeue();
			job.Execute();
		}
	};

	for(int i = 0; i < workerThreadsNum; ++i)
	{
		workerThreads.emplace_back(WorkerThread(workerThreadCallback));
	}
}

JobQueue& JobSystem::GetJobQueue()
{
	return jobQueue;
}

