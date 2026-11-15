# Build e execução no Spike

🌐 [Português](BUILD.md) · [English](../en/BUILD.md)

Porta da `doomgeneric` (GPL-2.0) para `-march=rv32im -mabi=ilp32`, sem A, sem C e sem unidade de ponto flutuante, executada no Spike com a WAD shareware v1.9 embutida.

## 1. Como obter as fontes e a WAD

```bash
./fetch.sh
```

O script clona `github.com/ozkl/doomgeneric` em `doomgeneric/` no commit `e257a09527062bfb986b6e5e0780839b6cbd5b71` e baixa a `doom1.wad` shareware de `github.com/Doom-Utils/shareware-collection` (pasta `Doom 1.9`) para `doom1.wad`. Em seguida confere os hashes da WAD.

| Item | Valor |
| :--- | :--- |
| Tamanho | 4196020 bytes |
| SHA-256 | `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771` |
| SHA-1 | `5b2e249b9c5133ec987b3ea77596381dc0d6bc1d` |

A WAD e a doomgeneric não são versionadas. A WAD v1.8 do `libsdl.org` não serve, porque a `demo1` é da versão 1.9.

## 2. Como compilar

```bash
./build.sh
```

O script executa no container `ghcr.io/insper-riscv/dev_tools:latest` e gera `build/doom.elf`.

| Variável | Padrão | Significado |
| :--- | :--- | :--- |
| `OPT` | `-O2` | Nível de otimização |
| `TARGET` | `spike` | `spike`, `layout` ou `board` (ver [PLACA.md](PLACA.md)) |
| `PLATFORM_DIR` | `Insper-RISC-V/src/cyclone-v/doom` | Pasta da plataforma `doom`, só no alvo `board` |
| `OUT` | `build` (`build-$TARGET` nos outros alvos) | Pasta de saída |
| `RAM_ORIGIN` | `0x40000000` | Origem da RAM |
| `RAM_LENGTH` | `0x04000000` | Comprimento da RAM (64 MiB) |
| `DEV_TOOLS_IMAGE` | `ghcr.io/insper-riscv/dev_tools:latest` | Imagem do container |

Para rodar já dentro de um container `dev_tools` com a pasta do projeto em `/w`, defina `DOOM_IN_CONTAINER=1`.

## 3. Como rodar no Spike

```bash
./run.sh
```

A execução leva cerca de 30 segundos, roda `doom -iwad doom1.wad -timedemo demo1`, grava a saída em `build/spike.out` e `build/spike.err` e termina com erro se o CRC-32 do último quadro diferir do gabarito.

## 4. Resultado de referência (gabarito do D1)

| Medida | Valor |
| :--- | :--- |
| Término | O `-timedemo` imprime `timed 5026 gametics in 5582 realtics` e encerra |
| Quadros entregues a `DG_DrawFrame` | 5065 |
| Instruções retiradas | 15994539968 |
| Instruções por quadro (média entre o primeiro e o último) | 3147534 |
| CRC-32 do último quadro (320x200 XRGB de 32 bits, bytes em ordem de memória) | `67371a4f` |
| Reprodutibilidade | Duas execuções geram saída idêntica |

O relógio virtual conta 100000 instruções por milissegundo (100 MHz com 1 instrução por ciclo), por isso o `fps` impresso pelo jogo (31,5) vale só para esse modelo.

## 5. Formato do binário e endereços

| Item | Valor |
| :--- | :--- |
| Formato | ELF32 RISC-V, carregado pelo Spike |
| RAM única (código, constantes, WAD, dados, bss, heap e pilha) | origem `RAM_ORIGIN`, comprimento `RAM_LENGTH` (`0x40000000` e 64 MiB) |
| `.text` / `.rodata` / `.wad` / `.data` / `.bss` | 241648 / 102952 / 4196020 / 59796 / 244056 bytes |
| Pilha | 64 KiB no topo da RAM |
| Origem e comprimento | símbolos `RAM_ORIGIN` e `RAM_LENGTH` em `port/link.ld`, definidos só ali |

## 6. Estrutura da porta

| Arquivo | Função |
| :--- | :--- |
| `port/doom_platform.c` | As funções `DG_*`, o relógio virtual, a contagem de quadros e o CRC-32 (ponto único de conversão do framebuffer) |
| `port/doom_wad_mem.c` | Backend de WAD em memória, com `memcpy`, sem `open`, `read` nem `lseek` |
| `port/doom_runtime.c` | Saída no console do Spike (HTIF) ou na JTAG UART (`DOOM_CONSOLE_UART`), stubs de sistema de arquivos, `_exit` com o resumo e o bloco de resultado, e o `main` |
| `port/link.ld` | Linker script de RAM única, com a WAD e o heap em endereços fixos nos alvos `layout` e `board` |
| `scripts/layout.sh` | Fonte única do mapa da SDRAM da placa |
