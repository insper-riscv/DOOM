# Medição do vídeo indexado na DE0-CV

## 1. O que muda no custo de um quadro

Na `main` (alvo `spike` e `layout` sem `VIDEO`), a doomgeneric converte cada quadro de 320x200 bytes de índice de paleta para XRGB de 32 bits com `cmap_to_fb`, rotina que responde por 60% das instruções do `-timedemo demo1`. Com `VIDEO=1` o build define `CMAP256`, a conversão deixa de existir e o quadro de índices segue direto para a janela do periférico `VIDEO` (paleta de 256 entradas RGB444 escrita só quando o DOOM a muda, framebuffer somente escrita). O código que faz isso é o do PR #4 e a medição por janelas é a do PR #3; esta página só traz os números.

## 2. Gabarito do Spike

Build `TARGET=layout VIDEO=1`, executado no Spike (`rv32im_zicsr_zicntr`), reconstruído a partir do commit do PR #4.

| Item | XRGB (sem `VIDEO`) | Indexado (`VIDEO=1`) |
| :--- | ---: | ---: |
| CRC-32 do último quadro | `67371a4f` | `7b28220e` |
| Quadros do timedemo | 5065 | 5065 |
| Instruções | 15,94 G | 8,5729 G |
| Instruções por quadro | 3.147.534 | 1.682.257 |

O CRC-32 `67371a4f` vale só para o formato XRGB e fica superado para o build com `VIDEO=1`, cujo gabarito passa a ser `7b28220e` (64.000 índices, sem a paleta).

## 3. Resultado na placa

Timedemo completo na DE0-CV, plataforma `doom`, relógio de 76,923 MHz, build `TARGET=board VIDEO=1 MEASURE=1` (sem espera de vsync e sem CRC, como manda a medição de CPI), contadores `cycle`, `instret` e Zihpm lidos a cada 100 quadros.

| Item | Placa |
| :--- | ---: |
| CRC-32 do último quadro | `7b28220e`, igual ao do Spike |
| Quadros do timedemo | 5057 (o Spike faz 5065 e a medição anterior 5041; a abertura depende do relógio) |

## 4. Antes e depois

Janelas de quadros 0 a 5000, do mesmo timedemo e do mesmo relógio. A base é a medição T10 (XRGB, CRC-32 `67371a4f`).

| Janela | Medida | Base (XRGB) | Indexado | Variação |
| :--- | :--- | ---: | ---: | ---: |
| 0 a 5000 | Instruções por quadro | 3.156.418 | 1.713.176 | -45,7% |
| 0 a 5000 | Ciclos por quadro | 9.570.668 | 7.154.461 | -25,2% |
| 0 a 5000 | CPI | 3,0321 | 4,1761 | +37,7% |
| 0 a 5000 | fps médio | 8,04 | 10,75 | +33,7% |
| 0 a 5000 | Ciclos em espera da RAM externa | 53,3% | 58,5% | +5,2 pontos |
| 0 a 100 | Instruções por quadro | 3.323.669 | 1.716.598 | -48,4% |
| 0 a 100 | CPI | 3,1142 | 4,2799 | +37,4% |
| 0 a 100 | fps | 7,43 | 10,47 | +40,9% |
| 100 a 5000 | Instruções por quadro | 3.153.005 | 1.713.106 | -45,7% |
| 100 a 5000 | CPI | 3,0304 | 4,1740 | +37,7% |
| 100 a 5000 | fps | 8,05 | 10,76 | +33,7% |

A espera da RAM é a soma, janela a janela, do contador de espera por RAM externa sobre os ciclos (o contador de 32 bits dá a volta em janelas longas, então cada janela de 100 quadros é somada separadamente).

## 5. Leitura

| Previsão do estudo T11 | Medido |
| :--- | :--- |
| Cerca de 1,32 M de instruções por quadro | 1,71 M (o Spike confirma 1,68 M, então a previsão ficou curta, não a placa) |
| Cerca de 19 fps por proporção das instruções | 10,75 fps |

O jogo perdeu 46% das instruções, mas o CPI subiu de 3,03 para 4,18: o que restou é o trabalho de renderização e de jogo, que o contador de espera mostra mais preso à SDRAM (58,5% dos ciclos contra 53,3%). O ganho em tempo é, portanto, de 25% dos ciclos por quadro e não de 46%, e a SDRAM já ocupa 58% dos ciclos. Reduzir o CPI pede menos acessos à SDRAM, não menos instruções.

## 6. Como foi medido

A medição na placa foi feita em 2026-11-18, com o binário `build-board-vm` (build `board` com `VIDEO=1 MEASURE=1`, a partir do PR #4 mais o relatório de 5 s e o reinício de inatividade, que ficam desligados no timedemo), e lida com `scripts/measure_report.py` sobre o `dump-ram` do bloco em `0x43000000`. O gabarito do Spike foi reconstruído e executado de novo para este documento.
