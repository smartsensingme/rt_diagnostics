/* SPDX-License-Identifier: Apache-2.0 */
#include <limits.h>
#include <string.h>

#include <rt_diagnostics/diagnostics.h>

#if RT_DIAG_ENABLED

/* Helper privado, chamado por record, cycle_begin_from_event, cycle_end e event. Soma saturante;
 * flag só transita para true. */
static void add(uint64_t *value, uint64_t amount, bool *saturated)
{
	if (UINT64_MAX - *value < amount) {
		*value = UINT64_MAX;
		*saturated = true;
	} else
		*value += amount;
}

/* Helper privado, chamado por cycle_end, stage_record e interval. Acumula distribuição e orçamento
 * sem divisão/float ou relógio próprio. */
static void record(struct rt_diag_distribution *v, uint64_t us, uint64_t budget, bool *sat)
{
	if (!v->count || us < v->min_us)
		v->min_us = us;
	if (us > v->max_us)
		v->max_us = us;
	add(&v->count, 1, sat);
	add(&v->total_us, us, sat);
	if (budget && us > budget)
		add(&v->over_budget, 1, sat);
}

/* API externa, sem chamadores internos. Copia configuração antes do reset para aceitar &d->config
 * como entrada. */
int rt_diag_init(struct rt_diag *d, const struct rt_diag_config *config, int64_t now_us)
{
	if (!d || !config || now_us < 0 || !config->window_us || config->window_us > INT64_MAX ||
	    config->stage_count > RT_DIAG_MAX_STAGES || config->event_count > RT_DIAG_MAX_EVENTS ||
	    config->interval_count > RT_DIAG_MAX_INTERVALS)
		return -EINVAL;

	struct rt_diag_config copy = *config;

	memset(d, 0, sizeof(*d));
	d->config = copy;
	d->window_start_us = now_us;
	d->last_cycle_end_us = now_us;
	d->next_window_id = 1;
	d->initialized = true;

	return 0;
}

/* Helper privado de cycle_begin e cycle_begin_from_event. Valida ordem e exclusão de ciclo antes de
 * publicar estado de trabalho. */
static int begin(struct rt_diag *d, int64_t now)
{
	if (!d || !d->initialized || now < d->window_start_us || now < d->last_cycle_end_us)
		return -EINVAL;
	if (d->cycle_open)
		return -EBUSY;
	d->work_start_us = now;
	d->latest_stage_end_us = now;
	d->cycle_open = true;
	d->from_event = false;
	d->wake_known = false;
	d->wake_us = 0;

	return 0;
}

/* API externa; delega ao helper begin sem assumir origem de evento. */
int rt_diag_cycle_begin(struct rt_diag *d, int64_t begin_us)
{
	return begin(d, begin_us);
}

/* API externa; valida origem antes de begin. Wake coalescido não permite reconstruir latência do
 * evento mais antigo. */
int rt_diag_cycle_begin_from_event(struct rt_diag *d, int64_t begin_us, int64_t latest_event_us,
				   uint32_t pending)
{
	if (!pending || latest_event_us < 0 || latest_event_us > begin_us)
		return -EINVAL;

	int rc = begin(d, begin_us);
	if (rc)
		return rc;
	d->from_event = true;
	d->wake_known = pending == 1;
	if (d->wake_known)
		d->wake_us = (uint64_t)(begin_us - latest_event_us);
	else {
		add(&d->window.coalesced_events, pending - 1, &d->window.saturated);
		add(&d->lifetime.coalesced_events, pending - 1, &d->lifetime.saturated);
		add(&d->window.ambiguous_wakes, 1, &d->window.saturated);
	}

	return 0;
}

/* API externa; valida tudo antes de acumular com record/add, mantendo estado intacto em erro. */
int rt_diag_cycle_end(struct rt_diag *d, int64_t end_us)
{
	if (!d || !d->initialized || !d->cycle_open || end_us < d->work_start_us ||
	    end_us < d->latest_stage_end_us)
		return -EINVAL;

	uint64_t work = (uint64_t)(end_us - d->work_start_us);
	record(&d->window.processing, work, 0, &d->window.saturated);
	uint64_t elapsed = work;
	if (d->wake_known) {
		record(&d->window.wake_latency, d->wake_us, 0, &d->window.saturated);

		/* Valid timestamps share a nonnegative int64 epoch, so sum <= INT64_MAX. */
		elapsed += d->wake_us;
		record(&d->window.cycle, elapsed, 0, &d->window.saturated);
	}
	if (d->config.deadline_us) {
		if (d->from_event && !d->wake_known)
			add(&d->window.deadline_unmeasured, 1, &d->window.saturated);
		else if (elapsed > d->config.deadline_us) {
			add(&d->window.deadline_overruns, 1, &d->window.saturated);
			add(&d->lifetime.deadline_overruns, 1, &d->lifetime.saturated);
		}
	}
	add(&d->window.cycles, 1, &d->window.saturated);
	add(&d->lifetime.cycles, 1, &d->lifetime.saturated);
	if (work > d->lifetime.max_processing_us)
		d->lifetime.max_processing_us = work;
	d->last_cycle_end_us = end_us;
	d->cycle_open = false;

	return 0;
}

/* API externa; contabiliza evento com add na janela e no histórico, inclusive fora de ciclo. */
int rt_diag_event(struct rt_diag *d, uint8_t id, uint64_t amount)
{
	if (!d || !d->initialized || id >= d->config.event_count)
		return -EINVAL;

	add(&d->window.events[id], amount, &d->window.saturated);
	add(&d->lifetime.events[id], amount, &d->lifetime.saturated);

	return 0;
}

/* API externa pura, sem chamadores internos: consulta prazo sem criar janelas artificiais. */
bool rt_diag_snapshot_due(const struct rt_diag *d, int64_t now_us)
{
	return d && d->initialized && now_us >= d->window_start_us &&
	       (uint64_t)(now_us - d->window_start_us) >= d->config.window_us;
}

/* API externa: valida, copia por valor e então limpa apenas a janela. Histórico e ciclo de IDs são
 * preservados. */
int rt_diag_take_snapshot(struct rt_diag *d, int64_t now_us, struct rt_diag_snapshot *out)
{
	if (!d || !d->initialized || !out || now_us < d->window_start_us ||
	    now_us < d->last_cycle_end_us)
		return -EINVAL;
	if (d->cycle_open)
		return -EBUSY;

	uint64_t id = d->next_window_id;
	if (id == UINT64_MAX)
		d->lifetime.saturated = true;
	else
		++d->next_window_id;

	*out = (struct rt_diag_snapshot){.window_id = id,
					 .start_us = d->window_start_us,
					 .end_us = now_us,
					 .duration_us = (uint64_t)(now_us - d->window_start_us),
					 .detailed = RT_DIAG_DETAILED,
					 .config = d->config,
					 .window = d->window,
					 .lifetime = d->lifetime};

	memset(&d->window, 0, sizeof(d->window));
	d->window_start_us = now_us;

	return 0;
}
#if RT_DIAG_DETAILED

/* API externa: mede etapa dentro do ciclo aberto; record não presume etapas mutuamente exclusivas.
 */
int rt_diag_stage_record(struct rt_diag *d, uint8_t id, int64_t start_us, int64_t end_us)
{
	if (!d || !d->initialized || !d->cycle_open || id >= d->config.stage_count ||
	    start_us < d->work_start_us || end_us < start_us)
		return -EINVAL;
	record(&d->window.stages[id], (uint64_t)(end_us - start_us), d->config.stage_budget_us[id],
	       &d->window.saturated);
	if (end_us > d->latest_stage_end_us)
		d->latest_stage_end_us = end_us;

	return 0;
}

/* API externa: record acumula duração fornecida; não gera tráfego nem deriva periodicidade. */
int rt_diag_interval(struct rt_diag *d, uint8_t id, uint64_t duration_us)
{
	if (!d || !d->initialized || id >= d->config.interval_count)
		return -EINVAL;
	record(&d->window.intervals[id], duration_us, 0, &d->window.saturated);

	return 0;
}
#endif
#endif
