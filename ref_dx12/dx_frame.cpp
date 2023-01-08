#include "dx_frame.h"


#include "dx_app.h"

void Frame::Init(int arrayIndexVal)
{
	Renderer& renderer = Renderer::Inst();

	arrayIndex = arrayIndexVal;

	renderer.CreateDepthStencilBuffer(depthStencilBuffer);
	Diagnostics::SetResourceName(depthStencilBuffer.Get(), "Depth Buffer Frame: " + std::to_string(arrayIndexVal));

	ViewDescription_t genericViewDesc = D3D12_DEPTH_STENCIL_VIEW_DESC();

	D3D12_DEPTH_STENCIL_VIEW_DESC& depthViewDesc = std::get<D3D12_DEPTH_STENCIL_VIEW_DESC>(genericViewDesc);
	depthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthViewDesc.Flags = D3D12_DSV_FLAG_NONE;
	depthViewDesc.Texture2D.MipSlice = 0;

	depthBufferViewIndex = renderer.dsvHeapAllocator->Allocate(depthStencilBuffer.Get(),
		&genericViewDesc);

	camera.Init();

	int drawAreaWidth = 0;
	int drawAreaHeight = 0;
	Renderer::Inst().GetDrawAreaSize(&drawAreaWidth, &drawAreaHeight);

	XMMATRIX sseYInverseAndCenterMat = XMMatrixIdentity();
	sseYInverseAndCenterMat.r[1] = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	sseYInverseAndCenterMat = XMMatrixTranslation(-drawAreaWidth / 2, -drawAreaHeight / 2, 0.0f) * sseYInverseAndCenterMat;

	XMStoreFloat4x4(&uiYInverseAndCenterMat, sseYInverseAndCenterMat);

	streamingCbvSrvAllocator = std::make_unique<std::remove_reference_t<decltype(*streamingCbvSrvAllocator)>>(
			Settings::CBV_SRV_DESCRIPTOR_HEAP_SIZE + arrayIndex * Settings::FRAME_STREAMING_CBV_SRV_DESCRIPTOR_HEAP_SIZE);
}

void Frame::ResetSyncData()

{
	DX_ASSERT(executeCommandListFenceValue != -1 && executeCommandListEvenHandle != INVALID_HANDLE_VALUE && 
		"Trying to reset frame's sync data. But this data is invalid");

	CloseHandle(executeCommandListEvenHandle);
	executeCommandListEvenHandle = INVALID_HANDLE_VALUE;

	executeCommandListFenceValue = -1;
}

std::shared_ptr<Semaphore> Frame::GetFinishSemaphore() const
{
	std::scoped_lock<std::mutex> lock(ownershipMutex);
	return frameFinishedSemaphore;
}

void Frame::Acquire()
{
	std::scoped_lock<std::mutex> lock(ownershipMutex);

	DX_ASSERT(frameFinishedSemaphore == nullptr && "Open frame error. Frame is not cleaned up.");
	DX_ASSERT(isInUse == false && "Trying to acquire frame that is still in use");

	isInUse = true;
	frameFinishedSemaphore = std::make_shared<Semaphore>(1);
}

void Frame::Release()
{
	std::scoped_lock<std::mutex> lock(ownershipMutex);

	DX_ASSERT(isInUse == true && "Trying to release frame, that's already released.");

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
	DX_ASSERT(arrayIndex != Const::INVALID_INDEX && "Trying to get frame index. But it is invalid");
	return arrayIndex;
}

Frame::~Frame()
{
	if (depthBufferViewIndex != Const::INVALID_INDEX)
	{
		Renderer::Inst().rtvHeapAllocator->Delete(depthBufferViewIndex);
	}

	if (executeCommandListEvenHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(executeCommandListEvenHandle);
	}
}

