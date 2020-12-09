#pragma once

#include "dx_buffer.h"
#include "dx_materialcompiler.h"
#include "dx_jobmultithreading.h"

class RenderStage_UI
{
public:

	void Init();
	
	void Execute(Context& context);

	Pass pass;
	BufferHandler stageMemory = BufConst::INVALID_BUFFER_HANDLER;
};