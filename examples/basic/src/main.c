/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>

#include <rt_diagnostics/diagnostics.h>
#include <rt_diagnostics/zephyr_clock.h>
#include <rt_log/log.h>

RT_LOG_MODULE_REGISTER(diag_example);

static struct rt_diag metrics;
static struct rt_diag_snapshot snapshot;

/** Zephyr application entry; external only. Initializes the owner, measures one
 * elapsed sleep per second and closes due windows. Calls collector APIs and
 * rt_log; failures return to Zephyr instead of using invalid snapshots.
 */
int main(void)
{
	const struct rt_diag_config config = {.window_us = 2000000, .event_count = 1};
	int rc = rt_diag_init(&metrics, &config, rt_diag_now_us());
	if (rc)
		return rc;

	for (;;) {
		rc = rt_diag_cycle_begin(&metrics, rt_diag_now_us());
		if (rc)
			return rc;

		k_msleep(2);
		rc = rt_diag_event(&metrics, 0, 1);
		if (rc)
			return rc;
		rc = rt_diag_cycle_end(&metrics, rt_diag_now_us());
		if (rc)
			return rc;

		int64_t now = rt_diag_now_us();
		if (rt_diag_snapshot_due(&metrics, now)) {
			rc = rt_diag_take_snapshot(&metrics, now, &snapshot);
			if (rc)
				return rc;

			RT_LOGI("EXAMPLE", "window=%llu duration_us=%llu cycles=%llu max_us=%llu",
				(unsigned long long)snapshot.window_id,
				(unsigned long long)snapshot.duration_us,
				(unsigned long long)snapshot.window.cycles,
				(unsigned long long)snapshot.window.processing.max_us);
		}
		k_msleep(1000);
	}
}
