#include "dx_buffer.h"

#ifdef max
#undef max

const BufferHandler Const::INVALID_BUFFER_HANDLER = std::numeric_limits<BufferHandler>::max();

#endif

void AssertBufferAndView::Lock()
{
	DX_ASSERT(locked == false && "Trying to lock buffer and view twice");

	locked = true;
}

void AssertBufferAndView::Unlock()
{
	DX_ASSERT(locked == true && "Trying to unlock assert buffer that is not locked");

	locked = false;
}
