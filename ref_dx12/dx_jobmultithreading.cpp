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

void GraphicsJobContext::CreateDependencyFrom(std::vector<GraphicsJobContext*> dependsFromList)
{
	assert(dependsFromList.empty() == false && "Trying to create dependency from empty list");
	assert(waitDependancy == nullptr && "Trying to create dependency to job that already has it");

	waitDependancy = std::make_shared<Semaphore>(dependsFromList.size());

	for (GraphicsJobContext* dependency : dependsFromList)
	{
		dependency->signalDependencies.push_back(waitDependancy);
	}
	
}

void GraphicsJobContext::SignalDependecies()
{
	for (std::shared_ptr<Semaphore>& dep : signalDependencies)
	{
		dep->Signal();
	}
}

Semaphore::Semaphore(int waitForValue):
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

void Semaphore::Wait()
{
	if (counter.load() < waitValue)
	{
		WaitForSingleObject(winSemaphore, INFINITY);
	}
}
