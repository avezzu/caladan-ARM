/*
 * ops.h - useful x86_64 instructions
 */

#pragma once

#include <features.h>
#include <base/types.h>
#include <zlib.h>

static inline void cpu_relax(void)
{
#if __GNUC_PREREQ(10, 0)
#  if __has_builtin(__builtin_ia32_pause)
	__builtin_ia32_pause();
#  endif
#else
	asm volatile("yield");
#endif
}


//this struct is never used
struct cpuid_info {
	unsigned int eax, ebx, ecx, edx;
};

static inline uint64_t rdtsc(void)
{
	uint64_t val;
  	asm volatile("mrs %0, CNTVCT_EL0" : "=r" (val));
	return val;
}

static inline uint64_t rdtscp(uint32_t *auxp)
{
	uint64_t ret;
	uint32_t c = 8;

  	asm volatile("mrs %0, CNTVCT_EL0" : "=r" (ret));

	if (auxp)
		*auxp = c;
	return ret;
}



static inline uint64_t __mm_crc32_u64(uint64_t crc, uint64_t val)
{	
	uint8_t bytes[8];
	*((uint64_t *)bytes) = val;
	
	return crc32(crc, bytes, 8);
}
