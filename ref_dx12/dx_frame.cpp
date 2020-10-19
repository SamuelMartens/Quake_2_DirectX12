#include "dx_frame.h"

#include<cassert>

#include "dx_jobmultithreading.h"
#include "dx_app.h"

void Frame::Init()
{

	Renderer& renderer = Renderer::Inst();

	renderer.CreateDepthStencilBuffer(depthStencilBuffer);
	
	depthBufferViewIndex = renderer.dsvHeap->Allocate(depthStencilBuffer);

	camera.Init();
}

void Frame::ResetSyncData()

{
	assert(executeCommandListFenceValue != -1 && executeCommandListEvenHandle != INVALID_HANDLE_VALUE && 
		"Trying to reset frame's sync data. But this data is invalid");

	CloseHandle(executeCommandListEvenHandle);
	executeCommandListEvenHandle = INVALID_HANDLE_VALUE;

	executeCommandListFenceValue = -1;
}

std::shared_ptr<Semaphore> Frame::GetFinishSemaphore() const
{
	return frameFinishedSemaphore;
}

Frame::~Frame()
{
	if (depthBufferViewIndex != Const::INVALID_INDEX)
	{
		Renderer::Inst().rtvHeap->Delete(depthBufferViewIndex);
	}

	if (executeCommandListEvenHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(executeCommandListEvenHandle);
	}
}

