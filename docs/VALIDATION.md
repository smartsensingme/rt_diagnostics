# Evidência e limites de validação

Baseline local: 29/10/2026. Compilador host clang/macOS, ASan/UBSan e testes do
repositório. Zephyr 4.4.99-ab92232cfcd, SDK 1.0.1, alvo nRF52840 promicro UF2 nos
consumidores usados na validação. Não é declaração de suporte testado a todas as
versões/arquiteturas Zephyr ou de baixo consumo elétrico medido.

- rt_diagnostics: completo/básico/off, ordem, IDs, coalescência, deadlines,
  saturação, janelas/histórico, ownership e relógio desligado. Núcleo C puro.
- rt_diagnostics_reporter: capacidades 1/2 e off; produtor/consumidor concorrentes,
  saída bloqueada, evicção, cópia ativa, wake, erros, payload/snapshot opcionais.
  Threads/locks/semafóros POSIX são doubles, não kernel Zephyr real.
- rt_log: filtros, limites, argumentos sem avaliação quando removidos e formatter
  cbprintf real do checkout Zephyr, incluindo float decimal/científico.

Nos consumidores físicos, telemetria/ACK/refresh e apresentação assíncrona
funcionaram. Stack do reporter: 696 usados de 3072 bytes na carga exercitada;
medição por pintura de stack inclui instrumentação, não prova pior caso formal.
O controlador USB da aplicação precisou margem adicional (fora destes módulos).
Teste sem terminal aprovado pelo usuário. Consumo elétrico foi explicitamente
adiado; ausência de polling/heap não comprova economia de todo o dispositivo.

O exemplo Zephyr é um build de integração sem gravação automática. Medir na placa
escolhida stack, carga máxima, backend indisponível e consumo antes de produção.
Testes host não reproduzem ABI ARM do empacotador, ISR/SMP ou todas as falhas físicas.
