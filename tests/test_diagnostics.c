/* SPDX-License-Identifier: Apache-2.0 */
#include <rt_diagnostics/diagnostics.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if !RT_DIAG_ENABLED
#include <rt_diagnostics/zephyr_clock.h>
#endif
/* Entrada externa do runner host. Valida instâncias reais do coletor nos modos
 * completo/básico/off; chama as APIs públicas e assert, sem kernel ou rádio.
 */
int main(void)
{
	struct rt_diag d = {0};
	struct rt_diag_snapshot out = {0};
	struct rt_diag_config config = {.window_us = 1000,
					.expected_period_us = 0,
					.deadline_us = 100,
					.stage_count = 2,
					.event_count = 3,
					.interval_count = 2,
					.stage_budget_us = {20, 0}};
#if !RT_DIAG_ENABLED
	memset(&d, 0xa5, sizeof(d));
	struct rt_diag before;
	memcpy(&before, &d, sizeof(d));
	assert(!rt_diag_init(&d, &config, 0));
	assert(!rt_diag_cycle_begin(NULL, 0));
	assert(!rt_diag_cycle_begin_from_event(NULL, -1, -1, 0));
	assert(!rt_diag_cycle_end(NULL, -1));
	assert(!rt_diag_stage_record(NULL, 255, -1, -1));
	assert(!rt_diag_interval(NULL, 255, UINT64_MAX));
	assert(!rt_diag_event(NULL, 255, UINT64_MAX));
	assert(!rt_diag_snapshot_due(&d, INT64_MAX));
	assert(rt_diag_take_snapshot(&d, 1000, &out) == -ENOTSUP);
	assert(rt_diag_now_us() == 0);
	assert(!memcmp(&before, &d, sizeof(d)));
	assert(!out.window_id);
	puts("PASS: disabled instrumentation is no-op, no clock/Zephyr dependency or snapshots");
#else
	assert(rt_diag_cycle_begin(&d, 0) == -EINVAL);
	assert(rt_diag_init(NULL, &config, 0) == -EINVAL);
	struct rt_diag_config bad = config;
	bad.stage_count = RT_DIAG_MAX_STAGES + 1;
	assert(rt_diag_init(&d, &bad, 0) == -EINVAL);
	bad = config;
	bad.event_count = RT_DIAG_MAX_EVENTS + 1;
	assert(rt_diag_init(&d, &bad, 0) == -EINVAL);
	bad = config;
	bad.interval_count = RT_DIAG_MAX_INTERVALS + 1;
	assert(rt_diag_init(&d, &bad, 0) == -EINVAL);
	bad = config;
	bad.window_us = 0;
	assert(rt_diag_init(&d, &bad, 0) == -EINVAL);
	assert(rt_diag_init(&d, &config, -1) == -EINVAL);
	assert(!rt_diag_init(&d, &config, 0));
	assert(rt_diag_cycle_begin_from_event(&d, 20, 10, 0) == -EINVAL);
	assert(rt_diag_cycle_begin_from_event(&d, 20, 21, 1) == -EINVAL);
	assert(!rt_diag_cycle_begin_from_event(&d, 20, 10, 1));
	assert(rt_diag_cycle_begin(&d, 20) == -EBUSY);
	assert(rt_diag_take_snapshot(&d, 30, &out) == -EBUSY && !out.window_id);
	assert(!rt_diag_stage_record(&d, 0, 25, 55));
	assert(!rt_diag_stage_record(&d, 1, 60, 70));
#if RT_DIAG_DETAILED
	assert(rt_diag_cycle_end(&d, 69) == -EINVAL);
	assert(rt_diag_stage_record(&d, 2, 25, 55) == -EINVAL);
	assert(rt_diag_stage_record(&d, 0, 19, 20) == -EINVAL);
#else
	struct rt_diag before;
	memcpy(&before, &d, sizeof(d));
	assert(!rt_diag_stage_record(&d, 255, -1, -1));
	assert(!rt_diag_interval(&d, 255, UINT64_MAX));
	assert(!memcmp(&before, &d, sizeof(d)));
#endif
	assert(!rt_diag_cycle_end(&d, 100));
	assert(rt_diag_cycle_end(&d, 100) == -EINVAL);
	assert(rt_diag_cycle_begin(&d, 99) == -EINVAL);
	assert(rt_diag_take_snapshot(&d, 99, &out) == -EINVAL);
	assert(!rt_diag_cycle_begin_from_event(&d, 150, 100, 1));
	assert(!rt_diag_cycle_end(&d, 250));
	assert(!rt_diag_cycle_begin_from_event(&d, 300, 290, 3));
	assert(!rt_diag_cycle_end(&d, 350));
	assert(!rt_diag_cycle_begin(&d, 400));
	assert(!rt_diag_cycle_end(&d, 410));
	assert(!rt_diag_event(&d, 0, 7));
	assert(!rt_diag_event(&d, 0, 1));
	assert(rt_diag_event(&d, 3, 1) == -EINVAL);
	assert(!rt_diag_interval(&d, 0, 30));
	assert(!rt_diag_interval(&d, 0, 40));
	assert(!rt_diag_snapshot_due(&d, 999));
	assert(rt_diag_snapshot_due(&d, 1000));
	assert(!rt_diag_take_snapshot(&d, 1000, &out));
	assert(out.window_id == 1 && out.duration_us == 1000 && out.start_us == 0 &&
	       out.end_us == 1000);
	assert(out.window.cycles == 4 && out.lifetime.cycles == 4);
	assert(out.window.processing.count == 4 && out.window.processing.total_us == 240);
	assert(out.window.processing.min_us == 10 && out.window.processing.max_us == 100);
	assert(out.window.wake_latency.count == 2 && out.window.wake_latency.total_us == 60);
	assert(out.window.cycle.count == 2 && out.window.cycle.total_us == 240);
	assert(out.window.deadline_overruns == 1 && out.window.deadline_unmeasured == 1);
	assert(out.window.coalesced_events == 2 && out.window.ambiguous_wakes == 1);
	assert(out.window.events[0] == 8 && out.lifetime.events[0] == 8);
	assert(out.lifetime.max_processing_us == 100 && !out.window.saturated);
#if RT_DIAG_DETAILED
	assert(out.detailed && out.window.stages[0].count == 1 &&
	       out.window.stages[0].over_budget == 1);
	assert(out.window.stages[0].total_us == 30 && out.window.stages[1].over_budget == 0);
	assert(out.window.intervals[0].count == 2 && out.window.intervals[0].total_us == 70);
	assert(out.window.intervals[0].min_us == 30 && out.window.intervals[0].max_us == 40);
#else
	assert(!out.detailed && !out.window.stages[0].count && !out.window.intervals[0].count);
#endif
	struct rt_diag_snapshot copy;
	memcpy(&copy, &out, sizeof(copy));
	assert(!rt_diag_event(&d, 0, 9));
	assert(!memcmp(&copy, &out, sizeof(out)));
	assert(!rt_diag_take_snapshot(&d, 2000, &out));
	assert(out.window_id == 2 && out.window.events[0] == 9 && out.lifetime.events[0] == 17);
	assert(!out.window.cycles && out.lifetime.cycles == 4 &&
	       out.lifetime.max_processing_us == 100);
	assert(!rt_diag_snapshot_due(&d, 1999));
	assert(rt_diag_take_snapshot(&d, 1999, &out) == -EINVAL);
	d.window.events[0] = UINT64_MAX - 1;
	d.lifetime.events[0] = UINT64_MAX - 1;
	assert(!rt_diag_event(&d, 0, 10));
	assert(d.window.events[0] == UINT64_MAX && d.window.saturated && d.lifetime.saturated);
#if RT_DIAG_DETAILED
	assert(!rt_diag_interval(&d, 0, UINT64_MAX));
	assert(!rt_diag_interval(&d, 0, 1));
	assert(d.window.intervals[0].total_us == UINT64_MAX && d.window.intervals[0].min_us == 1);
#endif
	d.next_window_id = UINT64_MAX;
	assert(!rt_diag_take_snapshot(&d, 3000, &out));
	assert(out.window_id == UINT64_MAX && out.lifetime.saturated);
	assert(!rt_diag_take_snapshot(&d, 4000, &out));
	assert(out.window_id == UINT64_MAX);
	assert(!out.window.saturated && out.lifetime.saturated);
	assert(!rt_diag_init(&d, &d.config, 5000));
	assert(d.config.window_us == 1000 && !d.lifetime.cycles && d.next_window_id == 1);
	assert(!rt_diag_cycle_begin(&d, 5000));
	assert(!rt_diag_cycle_end(&d, 5000));
	assert(!rt_diag_take_snapshot(&d, 5000, &out));
	assert(out.window.processing.count == 1 && !out.window.processing.total_us);
	config.deadline_us = 0;
	assert(!rt_diag_init(&d, &config, 0));
	assert(!rt_diag_cycle_begin_from_event(&d, 10, 0, 2));
	assert(!rt_diag_cycle_end(&d, 10000));
	assert(!rt_diag_take_snapshot(&d, 10000, &out));
	assert(!out.window.deadline_overruns && !out.window.deadline_unmeasured);
	printf("PASS: numerical collector detailed=%d; timing/order/limits, ambiguous wake, "
	       "windows/lifetime, immutable copies, saturation and disabled budgets\n",
	       RT_DIAG_DETAILED);
#endif
	return 0;
}
