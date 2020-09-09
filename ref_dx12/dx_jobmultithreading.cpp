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

	Job result = std::move(queue.back());
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
	//#DEBUG should acquire amount of hardware threads here
	const int workerThreadsNum = 3;

	// --- WORKER THREAD CALLBACK ---
	std::function<void()> workerThreadCallback = [this]()
	{
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

GraphicsJobContext::GraphicsJobContext(Frame& frameVal, CommandList& commandListVal):
	frame(frameVal),
	commandList(commandList)
{}
