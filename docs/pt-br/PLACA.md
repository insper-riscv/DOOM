# DOOM na placa (etapa D1)

🌐 [Português](PLACA.md) · [English](../en/BOARD.md)

A etapa D1 executa o mesmo `-timedemo demo1` do Spike na plataforma `doom` do [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V), na DE0-CV, sem tela. O jogo roda da SDRAM, o console sai pela JTAG UART e o último quadro é lido de volta com `dump-ram` e comparado com o CRC-32 `67371a4f` do Spike.

## 1. Alvos do build

| Alvo | Console | Relógio | WAD | Uso |
| :--- | :--- | :--- | :--- | :--- |
| `spike` (padrão) | HTIF | Virtual, por `instret` | Embutida no ELF | Gabarito da etapa D0 |
| `layout` | HTIF | Virtual, por `instret` | No ELF, em `WAD_ADDR` | Valida no Spike o código e o mapa de memória da placa |
| `board` | JTAG UART | `doom_clock.c` da plataforma | Fora do binário, por `load-ram --wad` | O binário da placa |

Os alvos `layout` e `board` compilam o mesmo código da porta, com as mesmas definições de endereços, e diferem só no console, no relógio e em onde a WAD entra. O `board` copia, pelo build, o `doom_clock.c` da plataforma (`Insper-RISC-V/src/cyclone-v/doom/runtime/`, baixado pelo `fetch.sh` em commit fixado) e usa o `memory_map.h` dela para o endereço da UART. Nada da plataforma é duplicado neste repositório. Outra cópia da plataforma serve com `PLATFORM_DIR`, desde que esteja dentro da pasta do projeto, que o container monta.

## 2. Mapa da SDRAM

Todos os endereços vêm de `scripts/layout.sh`, que o `build.sh`, o `run.sh` e o `image.sh` leem. A SDRAM começa em `0x40000000` e tem 64 MiB.

| Região | Endereço | Tamanho | Conteúdo |
| :--- | :--- | ---: | :--- |
| Imagem | `0x40000000` | 404.660 B de binário, cerca de 650 KiB com o bss | Código, constantes e dados iniciais, e o bss logo depois |
| WAD | `0x40100000` | 4.196.020 B | `doom1.wad`, carregada por `load-ram --wad --wad-address 0x40100000` |
| Framebuffer | `0x40580000` | 256.000 B | 320x200 XRGB de 32 bits, fixo para o `dump-ram` |
| Heap | `0x405C0000` | até a pilha | A zona do jogo e o `malloc` |
| Pilha | `0x43FEF000` a `0x43FFF000` | 64 KiB | Topo livre da SDRAM |
| Resultado | `0x43FFF000` | 64 B | Bloco de resultado (seção 4) |
| Palavras da plataforma | `0x43FFFBE0` a `0x43FFFFFF` | | `stdout`, `tohost`, `fromhost` e `launch`, do `platform.yaml` |

A WAD fica em `0x40100000` porque a imagem com o bss acaba antes de `0x40100000` (o linker confere) e porque o endereço coincide com o exemplo da documentação da plataforma. O framebuffer e o heap ficam acima do fim da WAD (`0x404001B4`), com folga para a versão 4,5 MiB de uma WAD maior. A última página da SDRAM (`0x43FFF000`) é a que a cache de dados ignora, então o bloco de resultado e a campainha sempre refletem a memória real.

O jogo não usa o buffer `stdout` da plataforma (1 KiB linear, que truncaria a saída longa do jogo). O console é a UART.

## 3. Console e relógio

A UART é a JTAG UART da plataforma (janela `0xC0000000`, `RV32_UART_BASE`), com o protocolo de `runtime/stdio.c` da plataforma. O estado fica em `+8`, e o programa só envia depois que um hospedeiro varreu a UART (bit 17), esperando lugar na fila por no máximo 200.000 leituras. Por isso o console precisa estar aberto (`riscv-tools console`) para o texto aparecer, e o resultado em si não depende dele. O jogo imprime o progresso a cada 100 quadros e, no fim, repete o resumo a cada 2 s.

`DG_GetTicksMs` é a do `doom_clock.c` da plataforma, a partir do CSR `time`, com 1.000 ticks por milissegundo. O jogo não depende dela para o resultado do `-timedemo`, que roda sem relógio de parede.

## 4. Bloco de resultado

No fim do `-timedemo` o jogo grava 11 palavras de 32 bits a partir de `0x43FFF000` e a magia por último. Ele zera a magia ao iniciar, então um resultado velho nunca vale.

| Palavra | Campo | Significado |
| ---: | :--- | :--- |
| 0 | `magic` | `0x53455244` (bytes `DRES`) quando o bloco está completo |
| 1 | `exit` | Código de saída do jogo (`-1` no fim do `-timedemo`) |
| 2 | `crc32` | CRC-32 (IEEE) do framebuffer, o gabarito `67371a4f` |
| 3 | `frames` | Quadros entregues (5065 no Spike) |
| 4 e 5 | `instret` | Instruções retiradas, parte baixa e alta |
| 6 | `per_frame` | Instruções por quadro (3.147.534 no Spike) |
| 7 | `fb_addr` | Endereço do framebuffer |
| 8 | `ms` | Milissegundos do `DG_GetTicksMs` ao terminar |
| 9 e 10 | `cycle` | Ciclos, parte baixa e alta (com o `instret`, dá o CPI) |

## 5. Preparar a placa

Estes passos são da plataforma e estão em [`docs/pt-br/platform/doom.md`](https://github.com/insper-riscv/Insper-RISC-V/blob/main/docs/pt-br/platform/doom.md) do Insper-RISC-V. Resumo.

1. Compilar e programar o `.sof` da plataforma `doom` na DE0-CV (`riscv-tools board` do Insper-RISC-V).
2. Gravar o carregador `runtime/doom_loader.S` na banca A da FLASH (seção 4.1 daquele documento). Ele espera a magia `0x4D4F4F44` em `launch` (`0x43FFFFF8`) e salta para `0x40000000`.

## 6. Comandos

`scripts/image.sh` compila o alvo `board`, confere que o binário cabe antes da WAD e imprime os comandos com os valores deste mapa. A execução completa é a seguinte, com `INSPER_RISC_V` apontando para um clone do Insper-RISC-V.

```bash
./fetch.sh
./scripts/image.sh
RT="riscv-tools --root $INSPER_RISC_V --config src/cyclone-v/doom/config.yaml"

$RT load-ram build-board/doom.bin --wad doom1.wad --wad-address 0x40100000
$RT console
$RT dump-ram build-board/result.mif --start-word 16776192 --words 16
$RT dump-ram build-board/fb.mif --start-word 1441792 --words 64000
uv run --no-project scripts/check_result.py build-board/result.mif --frame build-board/fb.mif --png build-board/frame.png
```

O `load-ram` zera `launch`, escreve o binário e a WAD pela porta de depuração da SDRAM, lê cada um de volta e só então escreve a magia. O `console` e o `dump-ram` usam o mesmo cabo JTAG e não rodam ao mesmo tempo: deixe o console até aparecer a linha `DOOM-D0: exit=-1 frames=... frame_crc32=...`, encerre com Ctrl-C e leia os dumps. O `check_result.py` confere a magia e o `crc32` do bloco com o do Spike e, com o framebuffer, recalcula o CRC-32 dos bytes lidos e grava o PNG do último quadro.

O `--start-word` do `dump-ram` é a distância à base da SDRAM em palavras, `(endereço - 0x40000000) / 4`, em decimal: 16776192 para o resultado e 1441792 para o framebuffer, de 64.000 palavras. A leitura do framebuffer pelo JTAG leva alguns minutos.

## 7. O que foi verificado sem a placa

| Verificação | Resultado |
| :--- | :--- |
| Alvo `spike` | CRC-32 `67371a4f`, 5065 quadros, igual ao gabarito |
| Alvo `layout` no Spike | Mesmo código e mapa da placa (WAD em `0x40100000`, framebuffer em `0x40580000`, heap em `0x405C0000`, pilha até `0x43FFF000`): CRC-32 `67371a4f`, 5065 quadros |
| Alvo `board` | Compila com `-Wall -Wextra -Werror` no `dev_tools` (GCC 16.1, picolibc), 404.660 bytes, sem `div` no relógio |
| Simulação GHDL da plataforma `doom` | Ver a seção 8 |

## 8. Simulação GHDL

O topo `doom_sim_test` roda o binário `board` com a BOOT_MEMORY e o carregador da banca A de verdade. O teste `scripts/sim/doom_image_run.py` lê a JTAG UART como o console da placa e é independente do testbench de programas em C do Insper-RISC-V.

| Passo | Comando, na raiz do Insper-RISC-V, no container `dev_tools` |
| :--- | :--- |
| Imagens (BOOT_MEMORY, FLASH com o carregador, SDRAM com o binário e a campainha em `0x40080000`) | `PYTHONPATH=. uv run --no-project --python /opt/riscv-tools/riscv-tools/bin/python ../DOOM/scripts/sim/gen_sim_images.py ../DOOM/build-board/doom.bin ../sim/nowad` |
| Teste | Copiar `doom_image_run.py` para `tests/python/unittests/rtl/`, criar a entrada `DoomImageRun` no `tests.json` (`cases` igual a `tests.python.unittests.rtl.doom_image_run:cases`, os três arquivos acima como `BOOT_MEMORY_FILE`, `ROM_FILE` e `SDRAM_FILE`) e rodar `uv run python tests/python/runner.py DoomImageRun`, com `DOOM_RUN_CYCLES` e `DOOM_EXPECT` no ambiente |

Resultado obtido, com os submódulos do Insper-RISC-V em `main` e o `doom_sim_top.vhd` portado para o formato novo (ver a seção 9):

| Medida | Valor |
| :--- | :--- |
| Velocidade | Cerca de 24.000 ciclos do clock base por segundo, ou 8.000 ciclos do núcleo (o núcleo anda a um terço do clock base) |
| Carregador | A BOOT_MEMORY confere a banca A, o carregador consome a campainha (palavra zerada) e salta para `0x40000000` |
| Primeira instrução da imagem | `auipc sp` leva `sp` a `0x43FFF000`, o topo da pilha do mapa |
| Cópia de `.data` e limpeza do bss | O `memset` do crt0 da picolibc é um laço de bytes, com um armazenamento de byte por iteração até a SDRAM, e leva mais de 8 milhões de ciclos do núcleo (mais de 5 minutos de GHDL), com o `pc` ainda nele nos dois pontos de amostra (4 e 8 milhões de ciclos) |
| Texto na UART | Nenhum nessa janela: o banner do jogo vem depois da limpeza do bss, fora do alcance prático do GHDL |

A simulação prova então a cadeia BOOT_MEMORY, carregador, salto e início da imagem do binário real, mas não chega ao console nem ao fim do `-timedemo`, que são provas da placa. Na placa a limpeza do bss leva cerca de 0,1 s.

## 9. Propostas ao Insper-RISC-V

1. Reservar o bloco de resultado no `platform.yaml` (`reserved.result: {base: 0x43FFF000, size: 64}`) e gerá-lo no `memory_map.h` (`RV32_DOOM_RESULT_ADDR`), em vez de um endereço fixo em `scripts/layout.sh`.
2. Atualizar o `doom_sim_top.vhd` ao formato novo de configuração (o do PR #152 ainda usa `ROM_EXTERNAL`, `RAM_EXTERNAL` e `PERIPH_SLOTS`, que o pacote gerado já não tem) e a lista `sim.vhdl_sources` da plataforma e do `DoomSimTop` (falta o `Peripherals/src/GPIO/soc/gpio_pkg.vhd`). Os ponteiros de submódulo de `main` também estão atrás dos submódulos (o `core` de `main` do Insper-RISC-V não tem `i_ext_error`, que o topo de simulação usa).
3. Opcional, para a medição da etapa D4, uma opção `--no-launch` no fluxo da documentação e um comando que espera a magia do resultado (`wait-ram`), para dispensar o console aberto durante a execução.

## 10. Riscos da primeira execução

| # | Risco | Efeito | Como reconhecer |
| :--- | :--- | :--- | :--- |
| 1 | Duração: cerca de 16 bilhões de instruções | Com CPI entre 2 e 4 a 77 MHz, de 7 a 14 minutos ou mais | Progresso a cada 100 quadros no console |
| 2 | Número de quadros diferente do Spike | A tela de abertura (melt) depende do relógio, então `frames` pode diferir de 5065 sem que o CRC-32 do último quadro mude | Comparar só o `crc32` |
| 3 | Imagem corrompida na SDRAM ou na leitura | CRC-32 diferente | `load-ram` confere a leitura de volta; um CRC diferente com a leitura correta indica bug do núcleo |
| 4 | Console sem varredura | Nenhum texto na tela | O bloco de resultado continua valendo |
| 5 | Cache de instruções | O carregador executa `fence.i` antes do salto | Travamento logo no início, sem texto |

## 11. Medicao de CPI por janelas de quadros

Com `MEASURE=1` no build (alvos `layout` e `board`), `DG_DrawFrame` grava, a cada 100 quadros entregues, um instantaneo de `cycle`, `instret` e, na placa, dos nove contadores de evento do Zihpm (indices 3 a 11: parada de mul/div, espera por RAM externa, desvio tomado, acesso desalinhado, falha da cache de instrucoes, falha da cache de dados, espera por FLASH, parada por load-use e operacao fundida) no bloco de `scripts/layout.sh` (`MEASURE_ADDR`, 0x43000000, 64 B de cabecalho e 64 instantaneos de 64 B). `MEASURE_STOP=N` encerra o jogo ao entregar o quadro N. O build sem `MEASURE` e identico ao anterior.

```bash
MEASURE=1 MEASURE_STOP=400 sh scripts/image.sh
# depois do load-ram e da execucao:
riscv-tools --root "$INSPER_RISCV" --config src/cyclone-v/doom/config.yaml dump-ram build-board/meas.mif --start-word 12582912 --words 1040
uv run --no-project scripts/measure_report.py build-board/meas.mif
```

No Spike (`TARGET=layout MEASURE=1`) o CRC-32 do ultimo quadro continua `67371a4f` e as instrucoes por quadro continuam 3.147.534.

## 12. Vídeo indexado e teclado PS/2

Com `VIDEO=1` no build (alvos `layout` e `board`), a doomgeneric é compilada com `CMAP256`: o jogo entrega 320x200 bytes de índice de paleta, o formato nativo do DOOM, em vez do XRGB de 32 bits. O alvo `board` liga então o periférico `VIDEO` (janela em `0x80100000`) e o `KBD` (`0x80007000`) da plataforma `doom`, e o alvo `layout` só calcula o CRC-32 dos quadros, para o Spike servir de gabarito.

| Variável | Padrão | Significado |
| :--- | :--- | :--- |
| `VIDEO` | 0 | 1 liga o framebuffer indexado e, no alvo `board`, o VIDEO e o KBD |
| `INTERACTIVE` | 0 | 1 executa `doom -iwad doom1.wad` (título, menu, jogo e demo de atração) em vez do `-timedemo demo1` |
| `VSYNC` | 1 | 0 tira a espera pelo início do apagamento vertical antes de escrever o quadro (com `MEASURE=1` o padrão é 0) |
| `VCRC_FRAMES` | 32 | Quantos quadros iniciais guardam o CRC-32 dos índices e da paleta (com `MEASURE=1` o padrão é 0) |
| `PROFILE` | 0 | 1 acrescenta à linha de 5 s uma linha `[PERFIL]` com os kciclos e as kinstruções por quadro de cada região do `DG_DrawFrame` (cópia, preparo, vsync, escrita e o resto) |

### 12.1. Vídeo

| Item | Valor |
| :--- | :--- |
| Quadro | `DG_DrawFrame` copia as 16.000 palavras de `DG_ScreenBuffer` para `0x80120000` com stores de palavra, quatro pixels cada, sem leitura antes da escrita |
| Paleta | 256 palavras em `0x80100400`, RGB444 com os 4 bits altos de cada canal de 8 bits, escritas só quando o DOOM chama `I_SetPalette` (`palette_changed`) |
| Sincronismo | O quadro só é escrito depois que o contador de quadros do VIDEO (`+8`) muda, isto é, no início do apagamento vertical; a escrita anda mais rápido que a varredura e não rasga a imagem |
| Ligar | O registrador de controle (`+0`) recebe 1 depois do primeiro quadro |

### 12.2. Teclado

O decodificador do conjunto 2 (`port/doom_kbd.c`) trata o prefixo `E0`, o `F0` de soltura, a repetição automática (uma tecla já pressionada não gera novo evento) e descarta `00`, `FA`, `AA`, `FE` e `FF`. Os eventos entram numa fila de 64 posições e `DG_GetKey` lê o KBD por sondagem a cada chamada. Um estouro da fila de 16 bytes do KBD solta todas as teclas.

| Tecla | Ação no jogo |
| :--- | :--- |
| W e S | Andar para a frente e para trás |
| A e D | Deslizar para a esquerda e para a direita |
| Setas esquerda e direita | Girar a câmera |
| Setas cima e baixo | Sem função no jogo |
| E | Usar, abrir portas e interruptores |
| Espaço, Ctrl esquerdo ou direito | Atirar |
| Shift | Correr |
| Alt (com as setas esquerda e direita) | Deslizar de lado |
| Nos menus: W, S, A, D e E | Cima, baixo, esquerda (diminui o controle deslizante), direita (aumenta) e confirmar (como o Enter) |
| Nos menus: as quatro setas, Enter, Esc | Navegar e escolher, como sempre |
| Nos menus: Y, N e as demais letras | Confirmações e atalhos do DOOM (N, O, L, Q...) sem mudança |
| Digitando o nome de um save | Todas as letras, WASD e E inclusive |
| Tab, Backspace, F1 a F12 | Mapa, apagar, as teclas do DOOM |
| Letras, dígitos, `-`, `=`, vírgula e ponto | ASCII, como os menus (`y` e `n`), os nomes de save e os cheats esperam |

As letras do movimento e do uso, e o espaço do tiro, são o ASCII que o decodificador entrega. O jogo as lê como `key_up`, `key_down`, `key_strafeleft`, `key_straferight`, `key_use` e `key_fire`, trocados em `I_InitInput` (`__wrap_I_InitInput`, depois do `M_LoadDefaults`). Por isso as letras seguem valendo na digitação de texto. O decodificador detecta o menu ativo com `menuactive` (doomstat.h) e a digitação do nome de um save com `saveStringEnter` (m_menu.c): com o menu ativo e sem digitação, W, S, A, D e E viram `KEY_UPARROW`, `KEY_DOWNARROW`, `KEY_LEFTARROW`, `KEY_RIGHTARROW` e `KEY_ENTER` no momento em que a tecla é pressionada, e a soltura entrega a mesma tecla do pressionamento, então abrir ou fechar o menu com a tecla pressionada não deixa nada preso.

O teste do decodificador roda no computador: `gcc -std=gnu17 -Wall -Wextra -Werror -DDOOM_KBD_HOST_TEST -Idoomgeneric/doomgeneric -Iport scripts/test_kbd.c port/doom_kbd.c -o test_kbd && ./test_kbd`.

### 12.3. Acompanhamento pela JTAG UART

A cada 5 s o jogo imprime uma linha com os quadros por segundo da janela, o total de quadros, quantas vezes a paleta foi escrita, as teclas pressionadas e soltas e o registro dos eventos recebidos (`+75e` é a seta para cima pressionada, `-75e` solta, `e` marca o prefixo estendido), e uma segunda com o CRC-32 dos índices dos quadros 1, 2, 4, 8, 16 e 32 e o da paleta.

O mesmo conteúdo fica na SDRAM, no bloco de estatísticas de `scripts/layout.sh` (`VSTAT_ADDR`, `0x43FFF100`, palavra 16776256 do `dump-ram`, com o cabeçalho e os pares de CRC descritos em `port/doom_video.h`), para quem lê sem console aberto.

### 12.4. Gabarito do Spike

No Spike (`TARGET=layout VIDEO=1`), o CRC-32 do último quadro do `-timedemo demo1` com os 64.000 índices é `7b28220e`, com 5065 quadros e 1.682.248 instruções por quadro (contra 3.147.534 do XRGB). Com `INTERACTIVE=1` os 32 primeiros quadros são a tela de título, de CRC-32 `b6290c41` e paleta `bc869b57`, e a placa deve imprimir os mesmos valores.

### 12.5. Reinício do jogo

Com `INTERACTIVE=1` o programa tem um reinício limpo: restaura o `.data` a partir da cópia tirada no início do `main` (`DOOM_DATA_BACKUP_ADDR`, `0x43800000`) e salta para o `_start`, que zera o bss e chama o `main` de novo. A WAD e o binário já estão na SDRAM e nada é recarregado. Três caminhos levam a ele.

| Causa | Mensagem na UART |
| :--- | :--- |
| Quit Game do menu (o `I_Quit` é envolvido por `--wrap`, porque o original volta ao jogo sobre o vídeo já liberado) | `[DOOM] Quit Game: reiniciando o jogo (titulo e menu)` |
| Fim do programa por erro (`I_Error`, `exit`), depois de 3 s | `[DOOM] fim do programa (codigo N): reiniciando em 3 s` |
| Inatividade | `[DOOM] Inatividade de 15 min: reiniciando o jogo` |

A inatividade conta pelo relógio do jogo (`DG_GetTicksMs`) o tempo sem nenhum byte do teclado (tecla pressionada, solta ou repetida). Passado `DOOM_IDLE_RESET_MS` (15 min, ou `IDLE_MS=N` em ms no `build.sh`), o jogo reinicia se estiver numa partida, no menu aberto, num intervalo entre fases ou no final. No título e no demo de atração, com o menu fechado, não faz nada. A contagem começa no primeiro quadro de cada boot, então não dispara durante o boot.

No Spike, `IDLE_MS=20000 TESTKEYS=3 VIDEO=1 INTERACTIVE=1 TARGET=layout` abre o menu no quadro 300 e aperta uma tecla no quadro 900: o jogo reinicia 20 s depois da última tecla (a 16,6 s de relógio, reinicia a 36,6 s) e não 20 s depois da abertura do menu.

## 13. Menu de escolha do jogo e as suas WADs

Com `MENU=1` (que pede `VIDEO=1` e `INTERACTIVE=1`) a placa mostra, antes do DOOM, um menu no estilo do jogo para escolher entre as WADs que o usuário forneceu. O build usa as WADs de uma pasta indicada pelo usuário, no lugar da `doom1.wad` shareware.

### 13.1. Indicar as WADs

```bash
./fetch.sh --wad-dir /caminho/das/wads
```

O `fetch.sh` guarda a pasta em `.wad-dir`, não baixa a `doom1.wad` e lista o que achou. A busca vai até quatro níveis abaixo, sem diferenciar maiúsculas de minúsculas, e procura `doom.wad`, `doom2.wad`, `plutonia.wad`, `tnt.wad` e as WADs do Master Levels (as de uma pasta `master`, com o `DOOM2.MAP` que diz o mapa de cada uma). Sem `--wad-dir`, o `fetch.sh` baixa a shareware como antes. A variável `WAD_DIR` tem o mesmo efeito para um build avulso.

| Variável | Padrão | Significado |
| :--- | :--- | :--- |
| `WAD_DIR` | a pasta de `.wad-dir` | Pasta das WADs |
| `WAD_SET` | `doom doom2 plutonia master` | Jogos do menu, em ordem: `doom`, `doom2`, `plutonia`, `tnt` e `master` |
| `MENU` | 0 | 1 liga o menu |

O Master Levels usa o `doom2.wad` como base, então `master` exige `doom2` no `WAD_SET`.

### 13.2. Quanto cabe na SDRAM

As WADs ficam em sequência a partir de `0x40100000`, cada uma alinhada a 4 KiB, num arquivo único `build-board/wads.bin`. Depois delas vêm o framebuffer e um heap de 7 MB, que têm de acabar antes de `0x43780000`. O `scripts/wads.sh` calcula o mapa e recusa um conjunto que não cabe, dizendo o que tirar.

| Conjunto | Tamanho das WADs | Cabe |
| :--- | ---: | :--- |
| `doom doom2 plutonia master` | 48,2 MB | Sim |
| `doom doom2 tnt master` | 49,0 MB | Sim |
| `doom doom2 plutonia tnt` | 62,6 MB | Não |

### 13.3. Carregar e usar

```bash
MENU=1 VIDEO=1 INTERACTIVE=1 ./scripts/image.sh
```

O `image.sh` imprime os comandos de sempre, com `--wad build-board/wads.bin --wad-address 0x40100000`: a carga é uma só, e o jogo acha cada WAD pelo nome numa tabela que o build embute.

O menu usa a arte da primeira WAD (fundo, caveira e fonte). Setas ou W e S movem, Enter, E ou espaço escolhem, e Esc volta. A escolha de um jogo abre o DOOM com `-iwad` dele. A escolha de uma fase do Master Levels abre o DOOM II com a WAD da fase e `-warp` no mapa certo, como o programa de carga do pacote faz. Ao sair do jogo (Quit Game, fim por erro ou inatividade), o programa reinicia e volta ao menu.

### 13.4. Conferir o menu no Spike

```bash
./scripts/menu_test.sh
```

O script compila o alvo `layout` com um roteiro de teclas (`MENU_SCRIPT`: `u` e `d` sobem e descem, `l` e `r` mudam de coluna, `e` escolhe, `b` volta, `p` guarda a tela), roda no Spike, confere a escolha devolvida e grava as telas em `build-layout/menu-png/`. Com `MENU_STOP=0` o jogo segue depois da escolha e abre a WAD escolhida.
