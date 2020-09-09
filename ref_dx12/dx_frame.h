#pragma once

#include <d3d12.h>
#include <vector>
#include <string>

#include "dx_common.h"
#include "dx_objects.h"
#include "dx_buffer.h"
#include "dx_drawcalls.h"
#include "dx_camera.h"

class Frame
{
public:

	Frame() = default;

	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;

	Frame(Frame&& other);
	Frame& operator=(Frame&& other);

	~Frame();

	void Init();
	void ResetSyncData();
	
	/* DON'T FORGET TO ADD TO ASSIGN MOVE WHEN ADD NEW MEMBERS */

	// Rendering related
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12CommandAllocator> commandListAlloc;
	
	// Used for rendering. Receive on frame beginning
	// Released on the frame end
	AssertBufferAndView* colorBufferAndView = nullptr;

	// Owned by frame
	ComPtr<ID3D12Resource> depthStencilBuffer;
	int depthBufferViewIndex = Const::INVALID_INDEX;

	// Not owned by frame, but rather receive on frame beginning
	// Released on the frame end
	std::vector<int> acquiredCommandListsIndices;
	
	// Utils
	bool isInUse = false;

	std::vector<DynamicObject> dynamicObjects;
	std::vector<ComPtr<ID3D12Resource>> uploadResources;
	std::vector<BufferHandler> streamingObjectsHandlers;

	std::vector<DrawCall_UI_t> uiDrawCalls;

	std::string currentMaterial;

	int frameNumber = Const::INVALID_INDEX;

	tagRECT scissorRect;
	//#DEBUG don't forget to update it in render frame dude!
	Camera camera;
	//#DEBUG so much stuff has to be cleaned up
	// don't forget to delete it later
	XMFLOAT4X4 uiProjectionMat;
	XMFLOAT4X4 uiViewMat;

	// Synchronization 
	int fenceValue = -1;
	HANDLE syncEvenHandle = INVALID_HANDLE_VALUE;

};