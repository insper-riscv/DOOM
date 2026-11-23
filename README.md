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

## 3. Escolher os jogos que vão para a SDRAM

Com o menu (`MENU=1`), a placa carrega as WADs que você escolher, e só elas aparecem no menu. A escolha é a variável `WAD_SET`, uma lista de jogos separados por espaço, na ordem em que aparecem. O padrão é `doom doom2 plutonia master`.

| Nome | Jogo | WAD procurada na pasta |
| :--- | :--- | :--- |
| `doom` | The Ultimate DOOM, 4 episódios | `doom.wad` |
| `doom2` | DOOM II | `doom2.wad` |
| `plutonia` | The Plutonia Experiment | `plutonia.wad` |
| `tnt` | TNT: Evilution | `tnt.wad` |
| `master` | Master Levels, 20 fases (usa o DOOM II como base) | As WADs da pasta `master`, com o `DOOM2.MAP` |

Aponte a pasta das suas WADs uma vez e monte a imagem com o `WAD_SET` desejado:

```bash
./fetch.sh --wad-dir /caminho/das/wads
WAD_SET="doom doom2 tnt master" MENU=1 VIDEO=1 INTERACTIVE=1 ./scripts/image.sh
```

O `image.sh` junta as WADs escolhidas em `build-board/wads.bin`, lista cada uma com o endereço e o SHA-256 e imprime o `load-ram` que as leva à SDRAM de uma vez. Para trocar de conjunto, rode o `image.sh` de novo com outro `WAD_SET` e carregue a nova imagem. Nem todo conjunto cabe na placa, como explica a seção 4. O uso completo do menu está em [docs/pt-br/PLACA.md](docs/pt-br/PLACA.md), seção 13.

## 4. A placa

O alvo `board` do `build.sh` gera o binário da plataforma `doom` do Insper-RISC-V. O console sai pela JTAG UART, o relógio vem do `doom_clock.c` da plataforma e a WAD é carregada na SDRAM por `riscv-tools load-ram`. `VIDEO=1` liga a saída VGA e `INTERACTIVE=1` liga o teclado PS/2 e o jogo completo; sem eles, o `-timedemo` deixa o resultado num bloco fixo da SDRAM, que o `dump-ram` lê. O mapa da SDRAM, os comandos e a conferência com o Spike estão em [docs/pt-br/PLACA.md](docs/pt-br/PLACA.md).

```bash
./fetch.sh && ./scripts/image.sh
```

### 4.1. A SDRAM de 64 MB e as WADs

A SDRAM da placa tem 64 MB, e as WADs dividem o espaço com a imagem do jogo, o framebuffer, um heap de 7 MB e a pilha. Sobram cerca de 49,5 MB para as WADs. Estes são os tamanhos das WADs originais v1.9:

| WAD | Tamanho |
| :--- | ---: |
| `doom.wad` (Ultimate DOOM) | 12,4 MB |
| `doom2.wad` | 14,6 MB |
| `plutonia.wad` | 17,4 MB |
| `tnt.wad` | 18,2 MB |
| Master Levels (as 20 fases juntas) | 3,8 MB |

Os quatro jogos juntos somam 62,6 MB e não cabem. Para colocar três, escreva os três no `WAD_SET` e some os tamanhos: o total tem de ficar abaixo de 49,5 MB. O `scripts/wads.sh` faz essa conta e recusa o conjunto que passa, dizendo o que tirar.

| Escolha | Total |
| :--- | ---: |
| `doom doom2 plutonia master` (padrão) | 48,2 MB |
| `doom doom2 tnt master` | 49,0 MB |
| `doom plutonia tnt` | 48,0 MB |

O Master Levels usa o `doom2.wad` como base, então o `master` só entra junto do `doom2`. O padrão leva o Ultimate DOOM, o DOOM II, o Plutonia e as 20 fases do Master Levels.

## 5. As teclas

### 5.1. Teclas do menu de escolha

| Tecla | O que faz |
| :--- | :--- |
| Seta para cima ou W | Item anterior |
| Seta para baixo ou S | Próximo item |
| Seta esquerda ou A | Na lista do Master Levels, vai para a coluna da esquerda |
| Seta direita ou D | Na lista do Master Levels, vai para a coluna da direita |
| Enter, E ou espaço | Escolhe o jogo; em "Master Levels", abre a lista; na lista, abre a fase |
| Esc ou Backspace | Na lista do Master Levels, volta à escolha do jogo |

### 5.2. Teclas do jogo

São as mesmas nos três jogos e no Master Levels.

| Tecla | O que faz |
| :--- | :--- |
| W | Anda para a frente |
| S | Anda para trás |
| A | Desliza para a esquerda |
| D | Desliza para a direita |
| Seta esquerda | Gira para a esquerda |
| Seta direita | Gira para a direita |
| E | Usa e abre portas |
| Espaço ou Ctrl | Atira |
| Shift | Corre |
| Alt com as setas esquerda e direita | Desliza de lado |
| 1 a 7 | Troca de arma |
| Tab | Mostra o mapa |
| Esc | Abre o menu do jogo |
| F10 e depois Y | Quit Game: volta ao menu de escolha do jogo |

Nos menus do próprio jogo as quatro setas navegam, e W S A D e E fazem o papel de cima, baixo, esquerda, direita e Enter, exceto na digitação do nome de um jogo salvo, onde são letras. As teclas de cima e de baixo não têm função durante a partida.

## 6. Contribuição

Cada alteração entra por Pull Request para a `main`. As regras gerais da organização estão em [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.md).

## 7. Licenças

O código deste repositório é Apache 2.0 (arquivo `LICENSE`). A `doomgeneric` e o código do DOOM são GPL-2.0 e não ficam neste repositório. O `fetch.sh` os baixa na compilação.

O binário resultante junta o jogo e a camada de plataforma e por isso é GPL-2.0. Quem o distribuir deve incluir o texto da GPL-2.0 e a oferta do código-fonte correspondente. Alterações na `doomgeneric`, se forem necessárias, entram como arquivos `.patch` sob GPL-2.0.

O `doom1.wad` shareware tem licença própria, que permite redistribuir apenas o pacote original sem modificação. Ele também não fica neste repositório.
