# Estudo de caches do DOOM sobre traces do Spike

[English](README.en.md)

## 1. Função

Duas ferramentas em C para estimar, sem a placa, o efeito de configurações de cache e de espera da SDRAM sobre o DOOM da plataforma `doom`. O Spike executa o `-timedemo demo1` e o `filter` reduz a saída de `--log-commits` a um trace binário compacto; o `sim` reproduz esse trace sobre um modelo de caches e de SDRAM e calcula CPI e quadros por segundo de cada configuração.

| Ferramenta | Entrada | Saída |
| :--- | :--- | :--- |
| `filter.c` | Saída de `--log-commits` do Spike (entrada padrão) | Um trace binário por janela de quadros (busca, load e store, um `uint32` por registro) e, em `stderr`, as contagens de classes de instrução por janela |
| `sim.c` | Os traces do `filter` e um conjunto de configurações `nome:chave=valor:...` | Uma tabela TSV com CPI, quadros por segundo, preenchimentos, escritas de volta e blocos M10K de cada configuração |

## 2. Limites do modelo

| Item | Valor |
| :--- | :--- |
| Regiões da SDRAM | Os limites de `region_of` e da cópia de paleta estão fixos no código para o `doom.elf` do alvo `layout` de um build específico; outro build exige conferir esses limites com `nm` |
| Modelo | Cada configuração tem cache de instruções e de dados (linha, associatividade LRU, política de escrita), regiões cacheáveis, memória interna fixa e buffer de escrita opcional; o tempo conta em ciclos do núcleo e a SDRAM é um recurso único em ordem |
| Dados | Os traces (alguns GB) e as tabelas de resultado do estudo original não fazem parte do repositório |

## 3. Uso

```bash
gcc -O2 -Wall -o filter scripts/cache_estudo/filter.c
gcc -O2 -Wall -Wno-unused -o sim scripts/cache_estudo/sim.c -lm
DPC=$(riscv32-unknown-elf-nm build-layout/doom.elf | grep " T DG_DrawFrame$" | cut -d' ' -f1)
spike --disable-dtb --isa=rv32im_zicsr_zicntr -m0x40000000:0x04000000 --log-commits build-layout/doom.elf 2>&1 >/dev/null \
  | ./filter "$DPC" 98:400:full.bin 2> extract.log
./sim -c "base" full.bin
```

A janela `98:400` conta os quadros pelas entradas de `DG_DrawFrame`, como o contador de eventos da placa. As chaves de configuração estão em `parse_cfg` de `sim.c`.

---

Copyright 2026 Insper. Licenciado sob a [Apache License, Version 2.0](../../LICENSE).
