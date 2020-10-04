#include "dx_buffer.h"

#ifdef max
#undef max

const BufferHandler BufConst::INVALID_BUFFER_HANDLER = std::numeric_limits<BufferHandler>::max();

#endif

void AssertBufferAndView::Lock()
{
	//#DEBUG
	if (locked == true)
	{
		int i = 0;
		++i;
	}
	//END

	assert(locked == false && "Trying to lock buffer and view twice");

	locked = true;
}

void AssertBufferAndView::Unlock()
{
	assert(locked == true && "Trying to unlock assert buffer that is not locked");

	locked = false;
}
