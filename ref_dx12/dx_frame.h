#pragma once

#include <d3d12.h>
#include <vector>
#include <string>
#include <memory>

#include "dx_common.h"
#include "dx_objects.h"
#include "dx_buffer.h"
#include "dx_drawcalls.h"
#include "dx_camera.h"
#include "dx_threadingutils.h"
#include "dx_texture.h"
#include "dx_pass.h"
#include "dx_framegraph.h"

class Semaphore;

// Frame is like a container that keep all jobs related to the same frame. 
// It also serves as a black board, i.e. it can be used for jobs to communicate between each other.
class Frame
{
public:

	Frame() = default;

	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;

	Frame(Frame&& other) = delete;
	Frame& operator=(Frame&& other) = delete;

	~Frame();

	void Init(int arrayIndexVal);
	void ResetSyncData();
	
	std::shared_ptr<Semaphore> GetFinishSemaphore() const;

	void Acquire();
	void Release();
	bool GetIsInUse() const;
	int GetArrayIndex() const;
	
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
	std::vector<entity_t> entitiesToDraw;
	std::vector<particle_t> particlesToDraw;

	std::vector<DynamicObject> dynamicObjects;
	LockVector_t<ComPtr<ID3D12Resource>> uploadResources;
	LockVector_t<BufferHandler> streamingObjectsHandlers;

	std::vector<DrawCall_UI_t> uiDrawCalls;
	
	std::vector<TextureCreationRequest_t> texCreationRequests;

	int frameNumber = Const::INVALID_INDEX;

	tagRECT scissorRect;
	Camera camera;
	XMFLOAT4X4 uiProjectionMat;
	XMFLOAT4X4 uiViewMat;

	// DirectX and OpenGL have different directions for Y axis,
	// this matrix is required to fix this. Also I am using wrong projection matrix
	// and need to fix whenever I will have a chance
	XMFLOAT4X4 uiYInverseAndCenterMat;

	// Synchronization 
	
	// These two values are used in the very end when we call ExecuteCommandList
	int executeCommandListFenceValue = -1;
	HANDLE executeCommandListEvenHandle = INVALID_HANDLE_VALUE;

	std::unique_ptr<FrameGraph> frameGraph;

private:

	std::shared_ptr<Semaphore> frameFinishedSemaphore;

	mutable std::mutex ownershipMutex;
	bool isInUse = false;

	int arrayIndex = Const::INVALID_INDEX;
};