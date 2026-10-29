# Exemplo básico Zephyr

Sem pinos, sensores ou dependência de placa específica. Requer um alvo Zephyr
com backend de console; selecione a placa no comando west build. Módulos irmãos
são o padrão, mas os caminhos podem ser passados por -DRT_LOG_DIR=... e demais
variáveis *_DIR do CMakeLists. Intervalos são ilustrativos, não perfil de energia.

```sh
source ~/.zephyr/exports.sh
west build -b SUA_PLACA -d build examples/basic
```

Executar o comando na raiz do módulo. Não grava automaticamente. O exemplo mede
tempo decorrido de um sleep de 2 ms, incluindo preempção; não mede CPU exclusiva.
Uma janela de 2 s fecha explicitamente no laço de 1 s. Toda API retorna erro
verificado. Snapshots pertencem à aplicação até serem copiados ao consumidor.
