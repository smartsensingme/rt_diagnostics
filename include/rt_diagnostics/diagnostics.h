/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RT_DIAGNOSTICS_H
#define RT_DIAGNOSTICS_H

/** @file
 * @brief Coleta numérica de tempo/eventos sem dependência do kernel no núcleo.
 *
 * APIs externas à biblioteca, sem chamadas entre APIs públicas. Todas as operações
 * de uma instância pertencem à mesma thread; transferir timestamps/contagens de
 * ISR com sincronização da aplicação. Timestamps int64_t não negativos, na mesma
 * base monotônica de us. Não há função de start/stop global, logger, tarefa ou timer.
 * Desabilitado: stubs não modificam estado; argumentos seguem avaliação normal C.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 1 se CONFIG_RT_DIAGNOSTICS habilita as APIs; caso contrário stubs inertes. */
#if defined(CONFIG_RT_DIAGNOSTICS) && CONFIG_RT_DIAGNOSTICS
#define RT_DIAG_ENABLED 1
#else
#define RT_DIAG_ENABLED 0
#endif

/** 1 se coleta e CONFIG_RT_DIAGNOSTICS_DETAILED_TIMING estão habilitados. */
#if RT_DIAG_ENABLED && defined(CONFIG_RT_DIAGNOSTICS_DETAILED_TIMING) &&                           \
    CONFIG_RT_DIAGNOSTICS_DETAILED_TIMING
#define RT_DIAG_DETAILED 1
#else
#define RT_DIAG_DETAILED 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Capacidade fixa de IDs de etapa; afeta layout/ABI das estruturas. */
#define RT_DIAG_MAX_STAGES 8

/** Capacidade fixa de IDs de evento; afeta layout/ABI das estruturas. */
#define RT_DIAG_MAX_EVENTS 8

/** Capacidade fixa de IDs de intervalo; afeta layout/ABI das estruturas. */
#define RT_DIAG_MAX_INTERVALS 4

/** Configuração copiada por init; tempos em us e IDs consecutivos a partir de zero. */
struct rt_diag_config {
	/** Janela nominal, 1..INT64_MAX us; não cria timer nem fecha janela sozinha. */
	uint64_t window_us;
	/** Metadado de período esperado; 0 significa orientado a eventos. Não calcula perdas. */
	uint64_t expected_period_us;
	/** Orçamento em us; 0 desabilita avaliação de deadline. */
	uint64_t deadline_us;
	/** Número de etapas válidas, 0..RT_DIAG_MAX_STAGES. */
	uint8_t stage_count;
	/** Número de eventos válidos, 0..RT_DIAG_MAX_EVENTS. */
	uint8_t event_count;
	/** Número de intervalos válidos, 0..RT_DIAG_MAX_INTERVALS. */
	uint8_t interval_count;
	/** Orçamento por etapa em us, 0 desabilita. Entradas fora de stage_count são ignoradas. */
	uint64_t stage_budget_us[RT_DIAG_MAX_STAGES];
};

/** Distribuição saturante; calcular média fora do caminho de coleta, somente com count > 0. */
struct rt_diag_distribution {
	/** Quantidade de amostras; satura em UINT64_MAX. */
	uint64_t count;
	/** Soma de durações em us, saturante. */
	uint64_t total_us;
	/** Menor duração em us; 0 quando nenhuma amostra foi registrada. */
	uint64_t min_us;
	/** Maior duração em us; 0 quando nenhuma amostra foi registrada. */
	uint64_t max_us;
	/** Quantidade de amostras estritamente acima do orçamento habilitado. */
	uint64_t over_budget;
};

/** Acumuladores da janela atual; zerados por take_snapshot. Índices não usados ficam zero. */
struct rt_diag_window {
	/** Ciclos concluídos, não notificações ou despertares presumidos. */
	uint64_t cycles;
	/** Soma de pending - 1 para ciclos com notificações coalescidas. */
	uint64_t coalesced_events;
	/** Ciclos com pending > 1, sem origem inequívoca de wake. */
	uint64_t ambiguous_wakes;
	/** Ciclos mensuráveis que excederam deadline_us. */
	uint64_t deadline_overruns;
	/** Ciclos ambíguos cuja avaliação de deadline habilitado foi omitida. */
	uint64_t deadline_unmeasured;
	/** Tempo decorrido begin..end; inclui preempções/interrupções, não CPU exclusiva. */
	struct rt_diag_distribution processing;
	/** Latência latest_event..begin apenas quando pending == 1. */
	struct rt_diag_distribution wake_latency;
	/** Tempo origem do evento..end somente quando origem é conhecida. */
	struct rt_diag_distribution cycle;
	/** Durações de etapas por ID; podem sobrepor-se, não somar como CPU total. */
	struct rt_diag_distribution stages[RT_DIAG_MAX_STAGES];
	/** Intervalos fornecidos pelo chamador, em us; não há clock interno. */
	struct rt_diag_distribution intervals[RT_DIAG_MAX_INTERVALS];
	/** Contadores de eventos por ID, incrementados explicitamente pelo chamador. */
	uint64_t events[RT_DIAG_MAX_EVENTS];
	/** true se algum acumulador desta janela saturou; não indica perda física. */
	bool saturated;
};

/** Histórico desde init; preservado ao fechar janela, sem armazenamento persistente. */
struct rt_diag_lifetime {
	/** Total saturante de ciclos concluídos. */
	uint64_t cycles;
	/** Total saturante das notificações coalescidas observadas. */
	uint64_t coalesced_events;
	/** Total saturante de deadlines mensuráveis excedidos. */
	uint64_t deadline_overruns;
	/** Maior duração begin..end observada desde init, em us. */
	uint64_t max_processing_us;
	/** Totais saturantes de eventos por ID. */
	uint64_t events[RT_DIAG_MAX_EVENTS];
	/** true quando histórico ou ID da janela saturou; reiniciado só por init. */
	bool saturated;
};

/** Cópia completa, sem ponteiros emprestados. Pode ser publicada a outro contexto após cópia
 * sincronizada. */
struct rt_diag_snapshot {
	/** ID da janela, começa em 1, cresce e satura em UINT64_MAX. */
	uint64_t window_id;
	/** Início inclusivo da janela, uptime/época monotônica fornecida pelo dono, não negativo.
	 */
	int64_t start_us;
	/** Instante do fechamento, mesma base de tempo; >= start_us. */
	int64_t end_us;
	/** Duração efetiva end_us - start_us, não o período nominal configurado. */
	uint64_t duration_us;
	/** true se a coleta foi compilada com detalhamento de etapas/intervalos. */
	bool detailed;
	/** Configuração por valor, sem ponteiros para a instância. */
	struct rt_diag_config config;
	/** Acumuladores fechados; outra janela já pode estar em coleta. */
	struct rt_diag_window window;
	/** Histórico observado no mesmo fechamento. */
	struct rt_diag_lifetime lifetime;
};

/** Armazenamento fixo do chamador. Campos privados de implementação: não editar diretamente.
 * Uma thread dona por instância; não ISR-safe, sem locks, alocação, I/O ou clock próprio. */
struct rt_diag {
	/** Cópia privada da configuração validada. */
	struct rt_diag_config config;
	/** Janela mutável da thread dona. */
	struct rt_diag_window window;
	/** Histórico mutável da thread dona. */
	struct rt_diag_lifetime lifetime;
	/** Início da janela atual, em us. */
	int64_t window_start_us;
	/** Início do ciclo aberto, em us. */
	int64_t work_start_us;
	/** Último fim de ciclo ou instante de init, para validar monotonicidade. */
	int64_t last_cycle_end_us;
	/** Maior fim de etapa registrada no ciclo aberto, em us. */
	int64_t latest_stage_end_us;
	/** ID saturante do próximo snapshot. */
	uint64_t next_window_id;
	/** Latência em us quando wake_known; zero nos demais casos. */
	uint64_t wake_us;
	/** true após init válido. */
	bool initialized;
	/** Exatamente um ciclo pode estar aberto por instância. */
	bool cycle_open;
	/** Ciclo atual foi aberto por begin_from_event. */
	bool from_event;
	/** Origem de wake inequívoca: pending == 1. */
	bool wake_known;
};

/** @brief Inicializa ou reinicia a instância, copiando configuração antes de zerar histórico.
 * Chamada externamente; internamente usa cópia por valor e memset, sem outra API.
 * @param d Armazenamento exclusivo, não NULL; não precisa estar inicializado.
 * @param config Configuração válida, emprestada pela chamada; pode ser &d->config.
 * @param now_us Instante não negativo de início da primeira janela, em us.
 * @return 0 ou -EINVAL (NULL, tempo negativo, janela fora de 1..INT64_MAX,
 * contagens acima das capacidades). Erro não altera d. Off retorna 0 sem mutação.
 */
#if RT_DIAG_ENABLED
int rt_diag_init(struct rt_diag *d, const struct rt_diag_config *config, int64_t now_us);
#else
static inline int rt_diag_init(struct rt_diag *d, const struct rt_diag_config *c, int64_t n)
{
	(void)d;
	(void)c;
	(void)n;
	return 0;
}
#endif

/** @brief Abre um ciclo sem origem de evento; deadline significa orçamento de processamento.
 * Chamada externa; internamente chama begin, que valida antes de alterar estado.
 * @param d Instância inicializada da thread dona, sem ciclo aberto.
 * @param begin_us Instante >= início da janela e último fim de ciclo, em us.
 * @return 0, -EINVAL por instância/ordem inválida, -EBUSY se ciclo aberto.
 * Erro preserva estado; off retorna 0 sem mutação. Custo constante.
 */
#if RT_DIAG_ENABLED
int rt_diag_cycle_begin(struct rt_diag *d, int64_t begin_us);
#else
static inline int rt_diag_cycle_begin(struct rt_diag *d, int64_t n)
{
	(void)d;
	(void)n;
	return 0;
}
#endif

/** @brief Abre ciclo com timestamps/contagem já sincronizados pelo chamador.
 * Chamada externa; chama begin/add. Não consulta interrupção ou relógio.
 * @param d Mesmas condições de cycle_begin; não ISR-safe.
 * @param begin_us Instante de início do trabalho, em us.
 * @param latest_event_us Instante não negativo <= begin_us, na mesma base.
 * @param pending Quantidade 1..UINT32_MAX; 1 permite medir latência de wake.
 * Valores >1 acumulam pending-1 coalescidos, marcam ambiguidade e omitem latência
 * e avaliação de deadline desse ciclo; não inventam origem do primeiro evento.
 * @return 0, -EINVAL ou -EBUSY. Erro preserva estado; off retorna 0. Custo constante.
 */
#if RT_DIAG_ENABLED
int rt_diag_cycle_begin_from_event(struct rt_diag *d, int64_t begin_us, int64_t latest_event_us,
				   uint32_t pending);
#else
static inline int rt_diag_cycle_begin_from_event(struct rt_diag *d, int64_t n, int64_t e,
						 uint32_t p)
{
	(void)d;
	(void)n;
	(void)e;
	(void)p;
	return 0;
}
#endif

/** @brief Fecha ciclo e atualiza distribuições, deadline e histórico saturante.
 * Chamada externa; usa record/add. Trabalho inclui preempções e interrupções.
 * @param d Instância inicializada com ciclo aberto, pertencente ao chamador.
 * @param end_us Instante >= início e fim de todas as etapas registradas, em us.
 * @return 0 ou -EINVAL por instância/ciclo/ordem inválida; erro não altera estado.
 * Off retorna 0 sem mutação. Custo constante; não faz divisão nem I/O.
 */
#if RT_DIAG_ENABLED
int rt_diag_cycle_end(struct rt_diag *d, int64_t end_us);
#else
static inline int rt_diag_cycle_end(struct rt_diag *d, int64_t n)
{
	(void)d;
	(void)n;
	return 0;
}
#endif

/** @brief Acumula evento na janela e histórico, dentro ou fora de um ciclo.
 * Chamada externa; usa add, sem relógio e sem amostragem automática.
 * @param d Instância inicializada, acesso exclusivo da thread dona.
 * @param id ID consecutivo menor que config.event_count.
 * @param amount Incremento 0..UINT64_MAX; zero é permitido, soma saturante.
 * @return 0 ou -EINVAL; erro não altera estado. Off retorna 0. Custo constante.
 */
#if RT_DIAG_ENABLED
int rt_diag_event(struct rt_diag *d, uint8_t id, uint64_t amount);
#else
static inline int rt_diag_event(struct rt_diag *d, uint8_t i, uint64_t a)
{
	(void)d;
	(void)i;
	(void)a;
	return 0;
}
#endif

/** @brief Consulta pura do vencimento da janela nominal; não fecha janela ou cria tarefa.
 * Chamada externa, sem helpers ou outras APIs internas.
 * @param d Instância inicializada, emprestada pela consulta, sem acesso concorrente.
 * @param now_us Instante na mesma base monotônica, em us.
 * @return true se now_us - window_start_us >= window_us; false para instância
 * inválida, instante anterior ao início ou modo off. Custo constante, sem mutação.
 */
#if RT_DIAG_ENABLED
bool rt_diag_snapshot_due(const struct rt_diag *d, int64_t now_us);
#else
static inline bool rt_diag_snapshot_due(const struct rt_diag *d, int64_t n)
{
	(void)d;
	(void)n;
	return false;
}
#endif

/** @brief Fecha janela explicitamente, mesmo antes de due, preservando histórico.
 * Chamada externa; copia estruturas e limpa janela com memset. Sem clock/formatter.
 * @param d Instância inicializada, sem ciclo aberto, acesso exclusivo.
 * @param now_us Instante >= início da janela e último fim de ciclo, em us.
 * @param out Buffer próprio, não NULL, que não pode sobrepor d. Recebe cópia sem
 * ponteiros; depois da publicação sincronizada pode pertencer a outro consumidor.
 * Avança ID saturante e início de janela. Em UINT64_MAX marca lifetime.saturated.
 * @return 0, -EINVAL por ponteiro/ordem, -EBUSY se ciclo aberto, -ENOTSUP off.
 * Erro preserva d e out. Custo fixo do layout (cópia/limpeza), sem malloc.
 */
#if RT_DIAG_ENABLED
int rt_diag_take_snapshot(struct rt_diag *d, int64_t now_us, struct rt_diag_snapshot *out);
#else
static inline int rt_diag_take_snapshot(struct rt_diag *d, int64_t n, struct rt_diag_snapshot *o)
{
	(void)d;
	(void)n;
	(void)o;
	return -ENOTSUP;
}
#endif

/** @brief Registra etapa de um ciclo aberto; etapas podem sobrepor-se.
 * Chamada externa; usa record e atualiza o maior fim de etapa do ciclo.
 * @param d Instância inicializada com ciclo aberto, thread dona.
 * @param id ID menor que config.stage_count.
 * @param start_us Início >= início do ciclo, em us.
 * @param end_us Fim >= start_us, na mesma base; cycle_end deverá sucedê-lo.
 * @return 0 ou -EINVAL; erro não altera estado. Básico/off retorna 0 sem mutação.
 * Custo constante; orçamento 0 desabilita contagem de ultrapassagem.
 */
#if RT_DIAG_DETAILED
int rt_diag_stage_record(struct rt_diag *d, uint8_t id, int64_t start_us, int64_t end_us);
#else
static inline int rt_diag_stage_record(struct rt_diag *d, uint8_t i, int64_t a, int64_t b)
{
	(void)d;
	(void)i;
	(void)a;
	(void)b;
	return 0;
}
#endif

/** @brief Registra duração fornecida, sem derivar período ou contar perdas automaticamente.
 * Chamada externa; usa record. Permitida dentro ou fora de ciclo.
 * @param d Instância inicializada, acesso exclusivo da thread dona.
 * @param id ID menor que config.interval_count.
 * @param duration_us Duração 0..UINT64_MAX us, contagens/soma saturantes.
 * @return 0 ou -EINVAL; erro preserva estado. Básico/off retorna 0 sem mutação.
 * Custo constante; não altera contadores de eventos nem adquire locks.
 */
#if RT_DIAG_DETAILED
int rt_diag_interval(struct rt_diag *d, uint8_t id, uint64_t duration_us);
#else
static inline int rt_diag_interval(struct rt_diag *d, uint8_t i, uint64_t t)
{
	(void)d;
	(void)i;
	(void)t;
	return 0;
}
#endif

#ifdef __cplusplus
}
#endif
#endif
