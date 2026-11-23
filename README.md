# DOOM no Insper-RISC-V

[English](README.en.md)

Porta do DOOM original (shareware, `doom1.wad`) para o processador RV32IM em pipeline do projeto [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V), na placa DE0-CV, com saída VGA e entrada por teclado PS/2.

## 1. O que este repositório contém

Só código escrito para este projeto, sob Apache 2.0.

| Item | Função |
| :--- | :--- |
| `fetch.sh` | Baixa a `doomgeneric` em um commit fixado e o WAD shareware, e confere o SHA-256 de cada um; com `--wad-dir PASTA` usa as suas WADs no lugar da shareware |
| `build.sh` | Compila a porta para `rv32im` com `ilp32` no container `dev_tools`, nos alvos `spike`, `layout` e `board` |
| `run.sh` | Executa o `-timedemo demo1` no Spike e confere o CRC-32 do último quadro |
| `port/` | Camada de plataforma (`DG_*`), backend de WAD em memória, runtime e linker script |
| `scripts/` | Funções comuns, o mapa da SDRAM (`layout.sh`), a imagem da placa (`image.sh`), a conferência do resultado (`check_result.py`) e a simulação GHDL (`sim/`) |
| `docs/` | Documentação de build e do gabarito, em `pt-br` e `en` |

## 2. Uso rápido

Requisitos no host: `git`, `curl`, `sha256sum` e `docker`.

```bash
./fetch.sh && ./build.sh && ./run.sh
```

O `build.sh` e o `run.sh` executam dentro do container `ghcr.io/insper-riscv/dev_tools:latest`. A execução termina com `CRC-32 confere com o gabarito: 67371a4f`. Os detalhes estão em [docs/pt-br/BUILD.md](docs/pt-br/BUILD.md).

## 3. Licenças

O código deste repositório é Apache 2.0 (arquivo `LICENSE`). A `doomgeneric` e o código do DOOM são GPL-2.0 e não ficam neste repositório. O `fetch.sh` os baixa na compilação.

O binário resultante junta o jogo e a camada de plataforma e por isso é GPL-2.0. Quem o distribuir deve incluir o texto da GPL-2.0 e a oferta do código-fonte correspondente. Alterações na `doomgeneric`, se forem necessárias, entram como arquivos `.patch` sob GPL-2.0.

O `doom1.wad` shareware tem licença própria, que permite redistribuir apenas o pacote original sem modificação. Ele também não fica neste repositório.

## 4. A placa

O alvo `board` do `build.sh` gera o binário da plataforma `doom` do Insper-RISC-V. O console sai pela JTAG UART, o relógio vem do `doom_clock.c` da plataforma e a WAD é carregada na SDRAM por `riscv-tools load-ram`. `VIDEO=1` liga a saída VGA e `INTERACTIVE=1` liga o teclado PS/2 e o jogo completo; sem eles, o `-timedemo` deixa o resultado num bloco fixo da SDRAM, que o `dump-ram` lê. O mapa da SDRAM, os comandos e a conferência com o Spike estão em [docs/pt-br/PLACA.md](docs/pt-br/PLACA.md).

```bash
./fetch.sh && ./scripts/image.sh
```

## 5. Escolher os jogos que vão para a SDRAM

Com o menu (`MENU=1`), a placa carrega as WADs que você escolher, e só elas aparecem no menu. A escolha é a variável `WAD_SET`, uma lista de jogos separados por espaço, na ordem em que aparecem.

| Nome | Jogo | WAD procurada na pasta |
| :--- | :--- | :--- |
| `doom` | The Ultimate DOOM, 4 episódios | `doom.wad` |
| `doom2` | DOOM II | `doom2.wad` |
| `plutonia` | The Plutonia Experiment | `plutonia.wad` |
| `tnt` | TNT: Evilution | `tnt.wad` |
| `master` | Master Levels, 20 fases (usa o DOOM II como base) | As WADs da pasta `master`, com o `DOOM2.MAP` |

Os quatro jogos juntos não cabem nos 64 MB da SDRAM com o heap do jogo, então escolha até três mais o `master`. O `scripts/wads.sh` calcula o mapa e recusa um conjunto que não cabe, dizendo o que tirar.

| `WAD_SET` | WADs na SDRAM | Cabe |
| :--- | ---: | :--- |
| `doom doom2 plutonia master` (padrão) | 48,2 MB | Sim |
| `doom doom2 tnt master` | 49,0 MB | Sim |
| `doom doom2 master` | 30,8 MB | Sim |
| `doom2` | 14,6 MB | Sim |
| `doom doom2 plutonia tnt` | 62,6 MB | Não |

Os tamanhos são os das WADs originais v1.9. Para escolher, aponte a pasta das suas WADs uma vez e monte a imagem com o `WAD_SET` desejado:

```bash
./fetch.sh --wad-dir /caminho/das/wads
WAD_SET="doom doom2 tnt master" MENU=1 VIDEO=1 INTERACTIVE=1 ./scripts/image.sh
```

O `image.sh` junta as WADs escolhidas em `build-board/wads.bin`, lista cada uma com o endereço e o SHA-256 e imprime o `load-ram` que as leva à SDRAM de uma vez. Para trocar de conjunto, rode o `image.sh` de novo com outro `WAD_SET` e carregue a nova imagem. O uso completo do menu está em [docs/pt-br/PLACA.md](docs/pt-br/PLACA.md), seção 13.

## 6. As telas do menu e como foram feitas

### 6.1. Telas e mudanças de tela

O programa mostra o menu antes do jogo e volta a ele quando o jogo termina. Cada tela troca por uma tecla, e a tabela abaixo é o mapa completo.

| Tela | O que mostra | Tecla | Vai para |
| :--- | :--- | :--- | :--- |
| Escolha do jogo | Título "CHOOSE YOUR GAME" e um item por jogo (título grande e descrição embaixo); o escolhido fica em vermelho com a caveira do DOOM piscando ao lado | Setas ou W e S | A mesma tela, com outro item |
| Escolha do jogo | O mesmo | Enter, E ou espaço num jogo | O jogo escolhido |
| Escolha do jogo | O mesmo | Enter, E ou espaço em "Master Levels" | Lista do Master Levels |
| Master Levels | 20 fases em duas colunas, cada uma com o nome e o mapa em que entra (`MAP01`, `MAP25`...) | Setas ou W e S | Outra fase da coluna |
| Master Levels | O mesmo | Setas esquerda e direita, ou A e D | A outra coluna |
| Master Levels | O mesmo | Esc ou Backspace | Escolha do jogo |
| Master Levels | O mesmo | Enter, E ou espaço | O DOOM II com a fase escolhida |
| Jogo | O DOOM, com os controles de sempre | Quit Game (F10 e Y), fim por erro (3 s depois) ou 15 minutos sem tecla | Escolha do jogo |

Escolher um jogo abre o DOOM com a WAD dele. Escolher uma fase do Master Levels abre o DOOM II com a WAD da fase já carregada, direto no mapa dela. Ao voltar do jogo, o menu reaparece do zero, na primeira tela.

### 6.2. Como foi feito

1. **Arte da própria WAD.** O menu não traz imagem nenhuma. Ele lê da primeira WAD da lista o fundo (a tela de título, escurecida por um dos mapas de luz da WAD), a caveira, a fonte pequena do jogo e a paleta de 256 cores. O repositório continua sem nenhum arquivo do DOOM.
2. **Uma tela de 320x200 de índices.** Cada tela é montada num buffer de 8 bits por pixel, o mesmo formato do jogo, e o texto é desenhado letra a letra com os recortes da fonte, ampliados 2 vezes nos títulos. O item escolhido usa as cores originais da fonte, que são avermelhadas. Os outros trocam cada cor por um cinza de brilho parecido, e as descrições usam um cinza mais escuro.
3. **Entrega ao vídeo.** O buffer e a paleta vão direto ao periférico de vídeo, do mesmo jeito que o jogo escreve um quadro. A caveira pisca a cada 250 ms, e a tela só é redesenhada quando algo muda.
4. **Teclado.** O menu lê os eventos do mesmo teclado PS/2 do jogo, sem driver novo.
5. **Da escolha ao jogo.** A escolha vira os argumentos do DOOM: o nome da WAD base e, no Master Levels, a WAD da fase e o mapa em que ela entra. As WADs ficam numa única imagem na SDRAM, com uma tabela de nome, endereço e tamanho, e o jogo abre cada uma pelo nome.
6. **Volta ao menu.** Quando o jogo termina, o programa restaura os dados iniciais e recomeça do início. Como o menu é a primeira coisa que o programa faz, ele reaparece sozinho, sem recarregar nada na SDRAM.
7. **Conferência sem a placa.** O `scripts/menu_test.sh` roda o menu no Spike com um roteiro de teclas, confere a escolha devolvida e salva cada tela como PNG em `build-layout/menu-png/`. As imagens não ficam no repositório porque mostram a arte do DOOM.

## 7. Contribuição

Cada alteração entra por Pull Request para a `main`. As regras gerais da organização estão em [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.md).
