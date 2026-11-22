# Teste de vídeo e teclado da plataforma doom

[English](README.en.md)

## 1. Função

Programa de bancada que exercita o periférico VIDEO e o teclado PS/2 (KBD) da plataforma `doom` sem o jogo, para separar um defeito de hardware, de cabo ou de teclado de um defeito da porta do DOOM.

| Parte | O que faz |
| :--- | :--- |
| Vídeo | Programa a paleta de 256 entradas e desenha, no framebuffer indexado de 320x200, oito barras de cores, uma rampa de cinza, uma moldura de 1 pixel, um quadrado que se move devagar e quatro caixas de seta que acendem enquanto a tecla correspondente está pressionada |
| Teclado | Lê o KBD por sondagem e imprime pela JTAG UART, para cada evento, a sequência de bytes do conjunto 2 (make, `F0` de soltura, `E0` de estendido), o nome da tecla e se foi pressionada ou solta |

O programa não usa biblioteca C: só acessa os registradores do VIDEO (`0x80100000`), do KBD (`0x80007000`) e da JTAG UART (`0xC0000000`), os endereços do `platform.yaml` da plataforma `doom`.

## 2. Arquivos

| Arquivo | Função |
| :--- | :--- |
| `vp_test.c` | O teste |
| `start.S` | Entrada: posiciona a pilha, zera o bss, chama `main` e, se voltar, estaciona o núcleo no laço da BOOT_MEMORY |
| `link.ld` | Liga código e dados a partir de `0x40000000`, com a pilha abaixo da última página da SDRAM |
| `build.sh` | Compila e gera `out/vp_test.bin` |

## 3. Uso

```bash
docker run --rm -v "$PWD":/w -w /w ghcr.io/insper-riscv/dev_tools:latest sh scripts/vp_test/build.sh
riscv-tools --root "$INSPER_RISCV" --config src/cyclone-v/doom/config.yaml load-ram scripts/vp_test/out/vp_test.bin
```

O carregador da FLASH (banca A) espera a palavra `launch`, executa `fence.i` e salta para `0x40000000`. A saída do teclado aparece no console da JTAG UART.

---

Copyright 2026 Insper. Licenciado sob a [Apache License, Version 2.0](../../LICENSE).
