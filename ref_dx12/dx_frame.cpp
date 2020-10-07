#include "dx_frame.h"

#include<cassert>

#include "dx_jobmultithreading.h"
#include "dx_app.h"

void Frame::Init()
{

	Renderer& renderer = Renderer::Inst();

	renderer.CreateCmdListAndCmdListAlloc(commandList, commandListAlloc);

	renderer.CreateDepthStencilBuffer(depthStencilBuffer);
	
	commandList->Close();

	depthBufferViewIndex = renderer.dsvHeap->Allocate(depthStencilBuffer);

	camera.Init();
}

void Frame::ResetSyncData()

{
	assert(fenceValue != -1 && syncEvenHandle != INVALID_HANDLE_VALUE && 
		"Trying to reset frame's sync data. But this data is invalid");

	CloseHandle(syncEvenHandle);
	syncEvenHandle = INVALID_HANDLE_VALUE;

	fenceValue = -1;
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

	if (syncEvenHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(syncEvenHandle);
	}
}

