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

	colorBufferAndView = other.colorBufferAndView;
	other.colorBufferAndView = nullptr;

	depthStencilBuffer = other.depthStencilBuffer;
	other.depthStencilBuffer = nullptr;

	depthBufferViewIndex = other.depthBufferViewIndex;
	other.depthBufferViewIndex = Const::INVALID_INDEX;

	acquiredCommandListsIndices = std::move(other.acquiredCommandListsIndices);

	dynamicObjects = std::move(other.dynamicObjects);
	
	uploadResources = std::move(other.uploadResources);

	streamingObjectsHandlers = std::move(other.streamingObjectsHandlers);

	uiDrawCalls = std::move(uiDrawCalls);

	isInUse = other.isInUse;
	other.isInUse = false;

	fenceValue = other.fenceValue;
	other.fenceValue = -1;

	syncEvenHandle = other.syncEvenHandle;
	other.syncEvenHandle = INVALID_HANDLE_VALUE;

	currentMaterial = std::move(other.currentMaterial);

	frameNumber = other.frameNumber;
	other.frameNumber = Const::INVALID_INDEX;

	scissorRect = other.scissorRect;
	
	camera = other.camera;

	uiProjectionMat = other.uiProjectionMat;

	uiViewMat = other.uiViewMat;

	return *this;
}

void Frame::Init()
{

	Renderer& renderer = Renderer::Inst();

	renderer.CreateCmdListAndCmdListAlloc(commandList, commandListAlloc);

	renderer.CreateDepthStencilBuffer(depthStencilBuffer);
	
	commandList->Close();

	depthBufferViewIndex = renderer.dsvHeap->Allocate(depthStencilBuffer);
}

void Frame::ResetSyncData()
{
	assert(fenceValue != -1 && syncEvenHandle != INVALID_HANDLE_VALUE && 
		"Trying to reset frame's sync data. But this data is invalid");

	CloseHandle(syncEvenHandle);
	syncEvenHandle = INVALID_HANDLE_VALUE;

	fenceValue = -1;
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

