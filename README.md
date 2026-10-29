# rt_diagnostics

Coletor numérico com integração Zephyr, mantido em repositório independente (versão local 0.1.0).
Implementação original em C, Apache-2.0; os componentes ESP-IDF do usuário serviram
como referência arquitetural, sem portar código ou depender de ESP-IDF/FreeRTOS.

## Responsabilidade e dependências

Acumula ciclos, processamento, latência de despertar quando conhecida, eventos,
etapas/intervalos e máximos; produz snapshots por valor. Não imprime, formata,
aloca, cria timer/thread/fila, controla rádio nem mede consumo elétrico.
Não há Meshtastic, placa, pinagem ou chip específico no núcleo C. O manifesto
Zephyr declara dependência de rt_log conforme a diretiva do projeto; o coletor
continua sem chamadas ao logger. Os testes numéricos compilam só C e libc.

## Configuração

- CONFIG_RT_DIAGNOSTICS=y: coleta habilitada.
- CONFIG_RT_DIAGNOSTICS_DETAILED_TIMING=y: etapas e intervalos habilitados.
- Desabilitado: stubs sem estado; due retorna false e take_snapshot -ENOTSUP.
- Básico: ciclos, eventos e históricos continuam; stage/interval viram no-ops.

rt_diag_config define window_us (>0), expected_period_us (metadado; 0=por eventos),
deadline_us (0 desabilita), contagens de IDs e stage_budget_us (0 desabilita).
Limites fixos: 8 etapas, 8 eventos, 4 intervalos. Nenhum nome/pointer fica no
caminho de coleta. Contadores e somas são uint64_t saturantes com flags explícitas;
IDs de janela também saturam em UINT64_MAX, sinalizando lifetime.saturated.

## Propriedade e relógio

Uma instância por thread dona. Nenhuma API de atualização é segura para uso
concorrente/ISR. A aplicação sincroniza timestamp/quantidade da ISR e passa valores
estáveis a cycle_begin_from_event. Todos os timestamps são int64_t não negativos,
monotônicos, na mesma base de microssegundos. Erros de ordem/ID/config retornam
-EINVAL e não alteram a instância; ciclo já aberto ou snapshot durante ciclo
retorna -EBUSY. init reinicia a instância inteira e preserva corretamente config
mesmo se config apontar para instance.config.

A fonte de tempo é fornecida pelo chamador. O adaptador opcional zephyr_clock.h
usa uptime ticks e converte para us; não cria timer. Na configuração atual de
32768 ticks/s a resolução é aproximadamente 30,5 us: unidade us não significa
precisão de 1 us, e trechos curtos podem medir zero. Ao desabilitar a coleta,
rt_diag_now_us retorna zero sem ler o relógio. Expressões com efeitos colaterais
passadas a funções inline continuam sujeitas à avaliação normal do C; evite-as.

## Ciclos e deadlines

cycle_begin/end medem tempo decorrido do trecho, incluindo interrupções e
preempções. Não equivale a CPU exclusiva. Sem origem de evento, deadline é orçamento
de processamento. Em begin_from_event com pending=1, a latência conhecida é
somada ao processamento para avaliar o deadline. pending>1 registra pending-1
notificações coalescidas e um despertar ambíguo; não fabrica latência do evento
mais antigo a partir do timestamp mais recente. Nesse caso a avaliação de
deadline é omitida e deadline_unmeasured é incrementado se há orçamento.
Contadores de notificações não provam perda física de amostras.

Stage timestamps devem estar dentro do ciclo aberto; end não pode preceder uma
etapa já registrada. Etapas podem sobrepor-se, portanto sua soma não é CPU total.
Eventos e intervalos podem ser registrados pela thread dona fora de um ciclo.

## Snapshots

snapshot_due é consulta pura. take_snapshot fecha explicitamente a janela, mesmo
antes de due se o chamador desejar, desde que não haja ciclo aberto. O resultado
contém config por valor, duração efetiva, estatísticas da janela, totais de vida
inteira e window_id crescente. Não há ponteiros emprestados. A janela é limpa,
mas histórico, contadores de vida inteira e máximo histórico são preservados.
O buffer de saída não pode sobrepor a instância. Depois da cópia, pode ser passado
a um consumidor sem compartilhar a instância mutável.

## Exemplo

```c
#include <rt_diagnostics/diagnostics.h>
#include <rt_diagnostics/zephyr_clock.h>

static struct rt_diag diag;
static struct rt_diag_snapshot result;

void init_diagnostics(void)
{
    struct rt_diag_config cfg = {
        .window_us = 5000000, .expected_period_us = 0,
        .deadline_us = 0, .stage_count = 1, .event_count = 1,
    };
    rt_diag_init(&diag, &cfg, rt_diag_now_us());
}

void process_work(void)
{
    rt_diag_cycle_begin(&diag, rt_diag_now_us());
    /* trabalho; medir etapas com timestamps fornecidos pela aplicação */
    rt_diag_event(&diag, 0, 1);
    rt_diag_cycle_end(&diag, rt_diag_now_us());
    int64_t now = rt_diag_now_us();
    if (rt_diag_snapshot_due(&diag, now)) {
        rt_diag_take_snapshot(&diag, now, &result);
        /* entregar cópia ao consumidor; não formatar dentro do coletor */
    }
}
```

## Integração com um produtor e um consumidor

A aplicação inicializa uma instância por dono, abre/fecha ciclos e acumula eventos.
No prazo escolhido pela aplicação, fecha explicitamente a janela e transfere uma
cópia ao consumidor. O rt_diagnostics_reporter é uma implementação opcional desse
consumidor; o coletor não depende de sua thread nem chama APIs de apresentação.

A periodicidade, nomes de etapas/eventos, sincronização de ISR e formato de saída
pertencem à aplicação. Não passar a instância mutável ao consumidor. Não presumir
ciclo periódico só porque há uma janela nominal; expected_period_us é metadado.

## Testes

python3 tests/run.py

Três modos (completo/básico/desabilitado), -Wall/-Wextra/-Werror, ASan/UBSan:
ordem de timestamps, IDs, eventos coalescidos, deadlines válidos/omitidos, saturação,
janelas/histórico, cópias independentes, reinit e ausência de clock no modo off.
O runner usa CC (clang por padrão) e descobre SDK via xcrun no macOS; em Linux
usa o compilador do ambiente. A validação executada nesta revisão foi no macOS.
Não exige checkout de aplicação, hardware, rt_log ou kernel para testes numéricos.

## Integração com apresentação e compatibilidade

Registre a pasta em ZEPHYR_EXTRA_MODULES antes de find_package(Zephyr), junto ao
rt_log declarado no manifesto. CMake inclui o cabeçalho sempre e compila o núcleo
somente com RT_DIAGNOSTICS=y. Não há função init global ou objetos escondidos:
o chamador aloca sua instância e os buffers de snapshot, sem malloc.

O reporter é opcional e separado; o núcleo continua C puro, sem logger, kernel,
I/O, thread, timer ou mutex. Código que precisa de concorrência deve copiar um
snapshot concluído e publicar; nunca entregar a instância mutável a outra thread.
As estruturas são dados locais do mesmo ABI, não wire protocol ou formato de
arquivo versionado. Sem mudança de API do coletor nesta revisão.

O primeiro baseline independente preserva nomes, ordem e tipos dos campos e
assinaturas C. Estruturas não são um ABI binário estável entre revisões arbitrárias;
recompile produtor e consumidor com o mesmo cabeçalho. Não houve remoção de API
pública do coletor. Veja CHANGELOG.md.

## Roteiro físico pendente

Compare a mesma carga com instrumentação off, básica e detalhada; meça tempo do
serviço e publicação com GPIO/logic analyzer ou clock adequado à resolução
necessária. Observe headroom de stack e máximos sob RX/ACK/refresh. Compare consumo
com/sem terminal conectado e com reporter desabilitado. Essas medições precisam
ser feitas no hardware; testes host e uso de esperas não provam redução de energia.

## Build C independente de Zephyr

```sh
cmake -S . -B build -DRT_DIAGNOSTICS_DETAILED=ON
cmake --build build
python3 tests/run.py
```

O target rt_diagnostics exporta include/, C11 e as definições de coleta/detalhe.
Para compilar diretamente, adicionar src/diagnostics.c e include/ e definir
CONFIG_RT_DIAGNOSTICS=1 e CONFIG_RT_DIAGNOSTICS_DETAILED_TIMING=0 ou 1 em todos os
consumidores. Não incluir zephyr_clock.h fora do Zephyr quando coleta está ativa;
fornecer timestamps próprios. Desabilitado pode usar só os stubs do cabeçalho.

## Exemplo Zephyr completo e dependências

Veja examples/basic (CMakeLists, prj.conf e src/main.c). Os módulos rt_log e
rt_diagnostics podem ser irmãos ou ter caminhos explícitos via *_DIR no CMake.
O manifesto declara rt_log como dependência de integração Zephyr, mas o
núcleo/target C puro e seus testes não o vinculam nem emitem logs.

Para reutilização: clonar este repositório no local escolhido, adicionar seu
caminho a ZEPHYR_EXTRA_MODULES antes de find_package(Zephyr) e fornecer rt_log pelo
mesmo mecanismo ou manifesto west. Fixe commits das dependências na aplicação;
não há download ou alteração de workspace dentro deste componente.

## Erros, recursos e decisões de tempo real

| Situação | Resultado |
|---|---|
| NULL/instância inválida, configuração/ID/ordem inválidos | -EINVAL, sem alterar estado/saída. |
| Abrir ciclo já aberto; snapshot durante ciclo | -EBUSY, sem substituir ciclo/janela. |
| Coleta desligada | Atualizações retornam 0 sem estado; due=false; snapshot=-ENOTSUP. |
| Detalhe desligado | Etapas/intervalos inertes; ciclos/eventos permanecem. |
| Contagem/soma/ID excedido | Saturação e flags; nenhum wrap ou perda inferida. |

Configuração inicial é copiada, assim como snapshots; nenhum ponteiro é retido.
Memória por instância é sizeof(struct rt_diag) e por saída sizeof(struct
rt_diag_snapshot), dependentes de ABI. Não há buffer oculto, malloc ou lock.
Atualizações são aritmética limitada; snapshot/init copiam/limpam layout fixo.
Callbacks, strings, floats, divisões e I/O ficam fora do coletor. Operações off
continuam sujeitas à avaliação normal de argumentos C: evitar efeitos colaterais.

Se durações pequenas aparecem como zero, conferir resolução do relógio. Se há
-EBUSY, fechar o ciclo antes do snapshot. Se intervalo usa base de tempo distinta,
corrigir a aplicação; não fabricar uma duração. Validar custo em hardware real.

## Evidência e manutenção

Veja docs/VALIDATION.md para escopo da validação, limitações e comandos. Código
sem pinagem ou placa; compatibilidade depende de C11 e, no adaptador opcional,
das APIs Zephyr de uptime. Ao alterar layout, atualizar dependentes e testes de
snapshot. Preservar saturação, semântica de deadlines e ausência de I/O no núcleo.

No macOS, se o CMake escolher um SDK incompatível com o compilador, definir
CMAKE_OSX_SYSROOT explicitamente para o SDK retornado pelo xcrun da instalação
Xcode ativa. Não embutir caminhos de SDK particulares no módulo.
