#pragma once

#include <d3d12.h>
#include <vector>

#include "dx_common.h"
#include "dx_objects.h"

class Frame
{
public:

	Frame() = default;

	Frame(const Frame&) = delete;
	Frame& operator=(const Frame&) = delete;

	Frame(Frame&& other);
	Frame& operator=(Frame&& other);

	~Frame();

	void Init(ComPtr<ID3D12Resource> newColorBuffer);
	
	/* DON'T FORGET TO ADD TO ASSIGN MOVE WHEN ADD NEW MEMBERS */
	bool isInUse = false;

	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<ID3D12CommandAllocator> commandListAlloc;
	//#DEBUG dunno if I need this
	ComPtr<ID3D12Fence> fence;
	
	//#DEBUG color buffer is binded to swap shain initially, so I can store it in
	// frame, same for depth buffer
	//#DEBUG I do need separate views, as I bind the in begine frame
	// views are just indices in the m_rtvHeap, and m_dsvHeap
	//#DEBUG Current back buffer also has state transition on begin frame and end frame
	ComPtr<ID3D12Resource> colorBuffer;
	ComPtr<ID3D12Resource> depthStencilBuffer;

	int colorBufferViewIndex = -1;
	int depthBufferViewIndex = -1;

	std::vector<DynamicObject> dynamicObject;


};