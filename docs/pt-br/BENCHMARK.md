# Benchmark do DOOM na DE0-CV

Este documento define como o desempenho do DOOM é medido na placa e registra os resultados do estado atual: fps, CPI e frequência. Os números foram medidos na DE0-CV com o SoC da plataforma `doom` do Insper-RISC-V, lendo as WADs do cartão SD.

## 1. Frequência e relação entre as grandezas

O núcleo RISC-V roda a **100 MHz**, e a busca de instruções (cache de instruções) roda no mesmo clock e na mesma fase. A SDRAM externa e o seu controlador rodam a **125 MHz**, num domínio separado. O timing fechou no `.sof` usado nas medidas:

| Domínio | Frequência | Folga de setup (pior canto) |
| :--- | ---: | ---: |
| Núcleo e busca | 100 MHz | +0,090 ns |
| Memória (SDRAM) | 125 MHz | +0,806 ns |

O hold é positivo em todos os cantos de temperatura e tensão. A relação entre as grandezas é

$$\text{fps} = \frac{f}{\text{CPI} \times I_q}$$

em que $f = 100$ MHz, $I_q$ é o número de instruções por quadro e CPI é o número médio de ciclos por instrução. O CPI vem dos contadores `mcycle` e `minstret` lidos a cada 100 quadros.

## 2. O que é medido

| Medida | Como sai | Para que serve |
| :--- | :--- | :--- |
| fps de uma janela de 100 quadros | Ciclos entre dois instantâneos dos contadores (a cada 100 quadros) | fps médio e CPI da janela |
| Ciclos de cada quadro | Diferença do `mcycle` entre duas chamadas de `DG_DrawFrame` | Quadro mais lento e janelas curtas |
| Quadro mais lento, pior janela de 5, 10 e 25 quadros | Calculado sobre os ciclos de cada quadro | Quedas rápidas de fps (o que o jogador sente como `lag`) |
| Quadros abaixo de 35 fps | Contagem sobre os quadros 101 até o último | Fração do tempo abaixo do ritmo original do DOOM |
| CRC-32 do último quadro e número de quadros | Bloco de resultado na SDRAM | Confere que a imagem é a mesma do Spike |

O fps medido é o do `-timedemo`: o jogo roda o demo sem esperar o relógio, então o valor é o que o hardware consegue, e não o que a tela mostra. Na partida normal o motor limita a 35 quadros por segundo.

As medidas pulam os primeiros 100 quadros, que incluem a leitura inicial dos WADs pelo cartão e rodam a 12 a 17 fps. O HUD de FPS fica desligado nas medições, porque desenhar no quadro muda as instruções contadas.

## 3. Como rodar

O `scripts/benchmark/bench.sh` monta a imagem de medição de cada demo (o jogo escolhido roda o demo e para no quadro 330), zera os blocos de medição, grava a imagem na placa, espera o fim e imprime o relatório. Ele reprograma a FPGA e escreve na SDRAM da placa.

| Variável | Significado |
| :--- | :--- |
| `INSPER_RISCV` | Raiz de um clone do Insper-RISC-V |
| `TOOLS` | Clone do Tools (roda o `riscv-tools` com `uv run --project`) |
| `SOF` | `.sof` do SoC DOOM a 100 MHz |
| `LOAD_FLASH` | `load_flash_nohold.py` da plataforma |
| `WAD_DIR` e `WAD_SET` | WADs e jogos do menu, iguais aos do cartão que está na placa |
| `SDFS` | `raw` (padrão) ou `fatfs`, igual ao cartão na placa |
| `DEMOS` | Lista `jogo:demo:nome:hud` (padrão: as medidas da seção 4) |
| `CABLE` | Cabo JTAG, se houver mais de uma placa |

```bash
INSPER_RISCV=~/Insper-RISC-V TOOLS=~/Tools SOF=doom_100MHz.sof \
LOAD_FLASH=~/Insper-RISC-V/load_flash_nohold.py WAD_DIR=~/doom/base \
  sh scripts/benchmark/bench.sh
```

Cada medida leva uns 15 minutos (a carga do primeiro quadro pelo cartão leva uns 40 segundos). Os dumps e os relatórios ficam em `build-bench/resultado/`. O `scripts/benchmark/analyze.py` também roda sozinho sobre os dumps. Os endereços da SDRAM são os de `scripts/layout.sh` e `port/doom_platform.h`: medição em `0x43780000`, ciclos por quadro 8 KiB depois, resultado em `0x43BFF000`.

## 4. Resultados do estado atual

Medidos a 100 MHz, com o cartão SD cru, o HUD desligado e a imagem final (`-O3 -funroll-loops -flto`). A coluna "janela" é o fps das janelas de 100 quadros, e as demais usam os ciclos de cada quadro, do 101 ao 329.

### 4.1. fps e CPI por janela de 100 quadros

| Jogo e demo | fps (101 a 200) | fps (201 a 300) | CPI (101 a 200) | CPI (201 a 300) |
| :--- | ---: | ---: | ---: | ---: |
| Ultimate DOOM `demo1` | 42,2 | 46,4 | 2,61 | 2,67 |
| DOOM II `demo1` | 40,8 | 39,7 | 2,90 | 2,86 |
| DOOM II `demo2` | 39,2 | 38,3 | 2,73 | 2,75 |
| Plutonia `demo1` | 39,2 | 33,7 | 2,83 | 2,79 |
| TNT `demo1` | 38,4 | 39,4 | 2,77 | 2,81 |
| DOOM II `demo3` | sem dado (bloco vazio) | sem dado | | |

O CPI do DOOM II `demo3` não foi lido (o bloco de instantâneos veio vazio nessa execução). Pelo Spike, o `demo3` executa 1.053.737 instruções por quadro, o que, com os 3,15 milhões de ciclos por quadro da placa, dá um CPI de cerca de 2,98.

### 4.2. fps por quadro

| Jogo e demo | Média | Quadro mais lento | Pior janela de 25 quadros | Quadros abaixo de 35 fps |
| :--- | ---: | ---: | ---: | ---: |
| DOOM II `demo2` | 38,5 | 31,5 | 36,3 | 15 de 229 (7 %) |
| DOOM II `demo2`, HUD de FPS ligado | 38,4 | 31,5 | 36,3 | 16 de 229 (7 %) |
| TNT `demo1` | 39,3 | 21,1 | 30,9 | 58 de 229 (25 %) |
| Plutonia `demo1` | 36,1 | 19,2 | 31,4 | 122 de 229 (53 %) |
| DOOM II `demo3` | 31,8 | 24,8 | 29,3 | 194 de 229 (85 %) |

O Ultimate DOOM `demo1` e o DOOM II `demo1` foram medidos só por janela, antes de a medição por quadro existir.

### 4.3. Leitura dos resultados

1. Nas cenas leves (Ultimate DOOM, DOOM II `demo1`) o jogo passa de 35 fps com folga, a 100 MHz.
2. O CPI fica entre 2,6 e 3,0, e a espera pela SDRAM externa é a maior parte dos ciclos. A frequência do núcleo (100 MHz) fixa o teto, e o CPI e as instruções por quadro decidem quanto do teto se usa.
3. Nas cenas pesadas (DOOM II `demo3`, Plutonia) há trechos abaixo de 35 fps, com quedas curtas até 19 a 25 fps. O `demo3` desenha mais colunas de parede por quadro, que é o laço mais caro do renderizador.
4. O contador de FPS desenhado direto no vídeo custa cerca de 0,1 % dos ciclos.
5. Os números do Spike para as instruções por quadro e o CRC do último quadro batem com os da placa, o que confirma que a imagem é a original.

As medidas foram feitas com os `.sof` de 100 MHz da plataforma `doom` (o do núcleo com folga de +0,514 ns e o final de +0,090 ns, que difere só nas bordas do vídeo), e o RTL do núcleo é o mesmo.
