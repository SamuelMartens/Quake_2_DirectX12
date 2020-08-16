#include "dx_frame.h"

#include<cassert>

#include "dx_app.h"
#include "dx_utils.h"

Frame::Frame(Frame&& other)
{
	*this = std::move(other);
}

Frame& Frame::operator=(Frame&& other)
{
	PREVENT_SELF_MOVE_ASSIGN;

	commandList = other.commandList;
	other.commandList = nullptr;

	commandListAlloc = other.commandListAlloc;
	other.commandListAlloc = nullptr;

	fence = other.fence;
	other.fence = nullptr;

	colorBuffer = other.colorBuffer;
	other.colorBuffer = nullptr;

	depthStencilBuffer = other.depthStencilBuffer;
	other.depthStencilBuffer = nullptr;

	colorBufferViewIndex = other.colorBufferViewIndex;
	other.colorBufferViewIndex = -1;

	depthBufferViewIndex = other.depthBufferViewIndex;
	other.depthBufferViewIndex = -1;

	dynamicObject = std::move(other.dynamicObject);

	isInUse = other.isInUse;
	other.isInUse = false;

	return *this;
}

void Frame::Init(ComPtr<ID3D12Resource> newColorBuffer)
{
	assert(newColorBuffer != nullptr && "Can't initialize frame with empty color buffer");

	Renderer& renderer = Renderer::Inst();

	renderer.CreateCmdListAndCmdListAlloc(commandList, commandListAlloc);
	renderer.CreateFences(fence);

	colorBuffer = newColorBuffer;
	colorBufferViewIndex = renderer.rtvHeapFrames->Allocate(colorBuffer);

	renderer.CreateDepthStencilBuffer(depthStencilBuffer);
	// Make sure depth buffer is ready for write
	commandList->ResourceBarrier(1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			depthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		));

	depthBufferViewIndex = renderer.dsvHeapFrames->Allocate(depthStencilBuffer);
}


Frame::~Frame()
{
	if (colorBufferViewIndex != -1)
	{
		Renderer::Inst().rtvHeapFrames->Delete(colorBufferViewIndex);
	}

	if (depthBufferViewIndex != -1)
	{
		Renderer::Inst().rtvHeapFrames->Delete(depthBufferViewIndex);
	}
}

