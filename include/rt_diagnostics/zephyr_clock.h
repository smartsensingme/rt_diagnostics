/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RT_DIAGNOSTICS_ZEPHYR_CLOCK_H
#define RT_DIAGNOSTICS_ZEPHYR_CLOCK_H
#include <rt_diagnostics/diagnostics.h>
#if RT_DIAG_ENABLED
#include <zephyr/kernel.h>

/** @brief Relógio opcional Zephyr para chamadas externas de instrumentação.
 * Chamado pela aplicação, não pelo núcleo numérico. Usa k_uptime_ticks e
 * k_ticks_to_us_floor64; retorna uptime em us, com resolução do tick do sistema.
 * Sem parâmetros, estado próprio, alocação ou timer; não cria precisão sub-tick.
 * Chamador deve usar esta mesma base em todos os timestamps de uma instância.
 * Off retorna 0 sem consultar kernel. Conversão vale enquanto uptime cabe int64_t.
 */
static inline int64_t rt_diag_now_us(void)
{
	return (int64_t)k_ticks_to_us_floor64(k_uptime_ticks());
}
#else
static inline int64_t rt_diag_now_us(void)
{
	return 0;
}
#endif
#endif
