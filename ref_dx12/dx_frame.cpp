#include "dx_frame.h"

#include<cassert>

#include "dx_jobmultithreading.h"
#include "dx_app.h"

void Frame::Init(int arrayIndexVal)
{
	Renderer& renderer = Renderer::Inst();

	arrayIndex = arrayIndexVal;

	renderer.CreateDepthStencilBuffer(depthStencilBuffer);
	
	depthBufferViewIndex = renderer.dsvHeap->Allocate(depthStencilBuffer.Get());

	camera.Init();

	int drawAreaWidth = 0;
	int drawAreaHeight = 0;
	Renderer::Inst().GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);

	XMMATRIX sseYInverseAndCenterMat = XMMatrixIdentity();
	sseYInverseAndCenterMat.r[1] = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	sseYInverseAndCenterMat = XMMatrixTranslation(-drawAreaWidth / 2, -drawAreaHeight / 2, 0.0f) * sseYInverseAndCenterMat;

	XMStoreFloat4x4(&uiYInverseAndCenterMat, sseYInverseAndCenterMat);
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

void Frame::Acquire()
{
	std::scoped_lock<std::mutex> lock(ownershipMutex);

	assert(frameFinishedSemaphore == nullptr && "Open frame error. Frame is not cleaned up.");
	assert(isInUse == false && "Trying to acquire frame that is still in use");

	isInUse = true;
	frameFinishedSemaphore = std::make_shared<Semaphore>(1);
}

void Frame::Release()
{
	std::scoped_lock<std::mutex> lock(ownershipMutex);

	assert(isInUse == true && "Trying to release frame, that's already released.");

	frameFinishedSemaphore->Signal();
	frameFinishedSemaphore = nullptr;
	isInUse = false;
}

bool Frame::GetIsInUse() const
{
	std::scoped_lock<std::mutex> lock(ownershipMutex);
	return isInUse;
}

int Frame::GetArrayIndex() const
{
	assert(arrayIndex != Const::INVALID_INDEX && "Trying to get frame index. But it is invalid");
	return arrayIndex;
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

