/*
 * preempt.h - support for kthread preemption
 */

#pragma once

#include <base/stddef.h>

extern __thread volatile unsigned int preempt_cnt;
extern void preempt(void);

/* this flag is set whenever there is _not_ a pending preemption */
#define PREEMPT_NOT_PENDING	(1 << 31)

/**
 * preempt_disable - disables preemption
 *
 * Can be nested.
 */
static inline void preempt_disable(void)
{
	preempt_cnt += 1;
	barrier();
}

/**
 * preempt_enable_nocheck - reenables preemption without checking for conditions
 *
 * Can be nested.
 */
static inline void preempt_enable_nocheck(void)
{
	barrier();
	preempt_cnt -= 1;
}

/**
 * preempt_enable - reenables preemption
 *
 * Can be nested.
 */
static inline void preempt_enable(void)
{
#ifndef __GCC_ASM_FLAG_OUTPUTS__
	preempt_enable_nocheck();
	if (unlikely(preempt_cnt == 0))
		preempt();
#else
	int zero;
	barrier();
	preempt_cnt -= 1;
	zero = (preempt_cnt == 0);
	if (unlikely(zero))
		preempt();
#endif
}

/**
 * preempt_needed - returns true if a preemption event is stuck waiting
 */
static inline bool preempt_needed(void)
{
	return (preempt_cnt & PREEMPT_NOT_PENDING) == 0;
}

/**
 * preempt_enabled - returns true if preemption is enabled
 */
static inline bool preempt_enabled(void)
{
	return (preempt_cnt & ~PREEMPT_NOT_PENDING) == 0;
}

/**
 * assert_preempt_disabled - asserts that preemption is disabled
 */
static inline void assert_preempt_disabled(void)
{
	assert(true);
}

/**
 * clear_preempt_needed - clear the flag that indicates a preemption request is
 * pending
 */
static inline void clear_preempt_needed(void)
{
	preempt_cnt = preempt_cnt | PREEMPT_NOT_PENDING;
}
