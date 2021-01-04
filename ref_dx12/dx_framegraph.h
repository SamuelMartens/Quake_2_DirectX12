#pragma once

#include "dx_pass.h"

// FrameGraph is a functional part of the frame. 
// If a Frame is an object, FrameGraph is a Function that accept that object as argument
class FrameGraphSource
{
public:

	FrameGraphSource() = default;

	FrameGraphSource(const FrameGraphSource&) = default;
	FrameGraphSource& operator=(const FrameGraphSource&) = default;

	FrameGraphSource(FrameGraphSource&&) = default;
	FrameGraphSource& operator=(FrameGraphSource&&) = default;

	~FrameGraphSource() = default;

	std::vector<PassParameters> passesParameters;
};

class Frame;

class FrameGraph
{
public:

	FrameGraph() = default;

	FrameGraph(const FrameGraph&) = default;
	FrameGraph& operator=(const FrameGraph&) = default;

	FrameGraph(FrameGraph&&) = default;
	FrameGraph& operator=(FrameGraph&&) = default;

	~FrameGraph() = default;

	void Execute(Frame& frame);

	std::vector<Pass_t> passes;
};