/*
 * ias_bw.c - the memory bandwidth subcontroller
 */

#include <base/stddef.h>
#include <base/log.h>

#include "defs.h"
#include "sched.h"
#include "ksched.h"
#include "ias.h"

/* statistics */
uint64_t ias_bw_punish_count;
uint64_t ias_bw_relax_count;
uint64_t ias_bw_sample_failures;
uint64_t ias_bw_sample_aborts;
float	 ias_bw_estimate;
float	 ias_bw_estimate_multiplier;

/* bandwidth threshold in cache lines per cycle for a single channel */
static float ias_bw_thresh;

struct pmc_sample {
	uint64_t gen;
	uint64_t val;
	uint64_t tsc;
};

static void ias_bw_throttle_core(int core)
{
        struct ias_data *sd = cores[core];
        sd->threads_limit = MAX(0, MIN(sd->threads_limit - 1,
				       sd->threads_active - 1));
        if (ias_add_kthread_on_core(core))
                ias_idle_on_core(core);
}

static void ias_bw_request_pmc(uint64_t sel, struct pmc_sample *samples)
{
	struct ias_data *sd;
	int core, tmp;

	sched_for_each_allowed_core(core, tmp) {
		sd = cores[core];
		if (!sd || sd->is_lc ||
		    bitmap_test(ias_ht_punished_cores, core)) {
			samples[core].gen = ias_gen[core] - 1;
			continue;
		}

		samples[core].gen = ias_gen[core];
		ksched_enqueue_pmc(core, sel);
	}
}

static void ias_bw_gather_pmc(struct pmc_sample *samples)
{
	int core, tmp;
	struct pmc_sample *s;

	sched_for_each_allowed_core(core, tmp) {
		s = &samples[core];
		if (s->gen != ias_gen[core])
			continue;
		if (!ksched_poll_pmc(core, &s->val, &s->tsc)) {
			s->gen = ias_gen[core] - 1;
			ias_bw_sample_failures++;
			continue;
		}
	}
}

static struct ias_data *
ias_bw_choose_victim(struct pmc_sample *start, struct pmc_sample *end,
		     unsigned int *worst_core)
{
	struct ias_data *sd, *victim = NULL;
	float highest_l3miss_rate = 0.0, bw_estimate;
	int core, tmp;

	/* zero per-task llc miss counts */
	ias_for_each_proc(sd)
		sd->bw_llc_miss_rate = 0.0;

	/* convert per-core llc miss counts into per-task llc miss counts */
	sched_for_each_allowed_core(core, tmp) {
		if (cores[core] == NULL ||
		    start[core].gen != end[core].gen ||
		    start[core].gen != ias_gen[core]) {
			continue;
		}

		bw_estimate = (float)(end[core].val - start[core].val) /
			      (float)(end[core].tsc - start[core].tsc);
		cores[core]->bw_llc_miss_rate += bw_estimate;
	}

	/* find an eligible task with the highest overall llc miss count */
	ias_for_each_proc(sd) {
		if (sd->threads_limit == 0 ||
		    sd->threads_limit <= sd->threads_guaranteed)
			continue;
		if (sd->bw_llc_miss_rate * sched_cores_nr < ias_bw_thresh)
			continue;
		if (sd->bw_llc_miss_rate <= highest_l3miss_rate)
			continue;

		victim = sd;
		highest_l3miss_rate = sd->bw_llc_miss_rate;
	}
	if (!victim)
		return NULL;

	/* find that task's core with the highest llc miss count */
	highest_l3miss_rate = 0.0;
	*worst_core = NCPU;
	sched_for_each_allowed_core(core, tmp) {
		bw_estimate = (float)(end[core].val - start[core].val) /
			      (float)(end[core].tsc - start[core].tsc);
		if (bw_estimate <= highest_l3miss_rate)
			continue;
		if (cores[core] != victim)
			continue;

		*worst_core = core;
		highest_l3miss_rate = bw_estimate;
	}
	if (*worst_core == NCPU)
		return NULL;

	start[*worst_core].gen = ias_gen[core] - 1;
	return victim;
}

static int ias_bw_punish(struct pmc_sample *start, struct pmc_sample *end)
{
	struct ias_data *sd;
	unsigned int core;

	/* choose the victim task */
	sd = ias_bw_choose_victim(start, end, &core);
	if (!sd)
		return -EAGAIN;
	sd->is_bwlimited = true;
	ias_bw_punish_count++;

	/* throttle the core */
	ias_bw_throttle_core(core);
	return 0;
}

static void ias_bw_relax(void)
{
	struct ias_data *sd, *best_sd = NULL;

	ias_for_each_proc(sd) {
		if (!sd->is_bwlimited || !sd->is_congested)
			continue;
		if (best_sd && sd->threads_limit <= best_sd->threads_limit)
			continue;

		best_sd = sd;
	}

	if (best_sd) {
		best_sd->threads_limit++;
		if (best_sd->threads_limit >= best_sd->threads_max)
			best_sd->is_bwlimited = false;
		ias_bw_relax_count++;
	}
}

static float ias_measure_bw(void)
{
	return 0;
}

enum {
	IAS_BW_STATE_RELAX = 0,
	IAS_BW_STATE_SAMPLE,
	IAS_BW_STATE_PUNISH,
};

static struct pmc_sample arr_1[NCPU], arr_2[NCPU];

/**
 * ias_bw_poll - runs the bandwidth controller
 */
void ias_bw_poll(void)
{
	
}

int ias_bw_init(void)
{
	
	
	return 0;

}
