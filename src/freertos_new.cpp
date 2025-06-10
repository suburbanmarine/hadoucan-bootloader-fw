
#include "FreeRTOS.h"

#include <new>

void* operator new(std::size_t size)
{
	void* const ptr = pvPortMalloc(size);

	if(ptr == nullptr)
	{
		#ifdef __cpp_exceptions
			throw std::bad_alloc();
		#else
			//Disable ISR, sync
			asm volatile(
				"cpsid i\n"
				"isb sy\n"
				"dsb sy\n"
				: /* no out */
				: /* no in */
				: "memory"
			);

			for(;;)
			{

			}
		#endif
	}

	return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t& tag) noexcept
{
	return pvPortMalloc(size);
}

void operator delete(void* ptr) noexcept
{
	vPortFree(ptr);
}