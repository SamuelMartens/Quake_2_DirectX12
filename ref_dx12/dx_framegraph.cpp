#include "dx_framegraph.h"

#include "dx_app.h"
#include "dx_jobmultithreading.h"

void FrameGraph::Execute(Frame& frame)
{
	ASSERT_MAIN_THREAD;

	Renderer& renderer = Renderer::Inst();
	JobQueue& jobQueue = JobSystem::Inst().GetJobQueue();

	// Some preparations
	XMMATRIX tempMat = XMMatrixIdentity();
	XMStoreFloat4x4(&frame.uiViewMat, tempMat);

	tempMat = XMMatrixOrthographicRH(frame.camera.width, frame.camera.height, 0.0f, 1.0f);
	XMStoreFloat4x4(&frame.uiProjectionMat, tempMat);

	//#TODO get rid of begin frame hack
	// NOTE: creation order is the order in which command Lists will be submitted.
	// Set up dependencies 

	std::vector<JobContext> framePassContexts;
	//#DEBUG this is not even frame pass context
	JobContext& beginFrameContext = framePassContexts.emplace_back(renderer.CreateContext(frame));

	for (Pass_t& pass : passes)
	{
		framePassContexts.emplace_back(renderer.CreateContext(frame));
	};

	JobContext endFrameJobContext = renderer.CreateContext(frame);
	endFrameJobContext.CreateDependencyFrom(framePassContexts);

	jobQueue.Enqueue(Job([ctx = framePassContexts[0], &renderer]() mutable
	{
		renderer.BeginFrameJob(ctx);
	}));

	for (int i = 0; i < passes.size(); ++i)
	{
		std::visit([
			&jobQueue,
				// i + 1 because of begin frame job
				passJobContext = framePassContexts[i + 1]](auto&& pass)
			{
				jobQueue.Enqueue(Job(
					[passJobContext, &pass]() mutable
				{
					JOB_GUARD(passJobContext);

					pass.Execute(passJobContext);
				}));

			}, passes[i]);
	}

	jobQueue.Enqueue(Job([endFrameJobContext, &renderer, this]() mutable
	{
		renderer.EndFrameJob(endFrameJobContext);
	}));
}