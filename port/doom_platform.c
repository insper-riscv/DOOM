/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Camada de plataforma da doomgeneric: as cinco funcoes DG_*.
 *
 * Versao de referencia para o Spike, sem tela nem teclado. Cada funcao e o
 * ponto onde a placa liga o seu hardware depois:
 *  - DG_Init e DG_SleepMs: nada a fazer no Spike;
 *  - DG_GetTicksMs: le o CSR `time` (Zicntr); na placa o divisor do tick fixa
 *    DOOM_TIME_TICKS_PER_MS;
 *  - DG_GetKey: sem teclado; o teclado PS/2 (scancodes set 2) entra aqui e
 *    devolve pressed e o codigo da tecla do DOOM;
 *  - DG_DrawFrame: conta os quadros e deixa o buffer para a conversao de
 *    formato (doom_frame_convert), o ponto unico onde o framebuffer da placa
 *    (direto 3 x bits por canal ou indexado de 8 bits com paleta) sera gerado.
 */

#include <stdint.h>
#include <stdio.h>

#include "doomgeneric.h"
#include "doom_platform.h"

/*
 * Ticks do CSR `time` por milissegundo. O `time` do Spike avanca 1 tick a
 * cada 100 instrucoes (mtime = instret / 100), entao 1000 ticks por ms
 * equivalem a um Core de 100 MHz com 1 instrucao por ciclo. Com o divisor
 * de 10 MHz que o Spike anuncia, o jogo esperaria 1 milhao de instrucoes por
 * milissegundo nas esperas de tempo (melt, DG_SleepMs) e o -timedemo nao
 * terminaria em tempo util.
 */
#ifndef DOOM_TIME_TICKS_PER_MS
#define DOOM_TIME_TICKS_PER_MS 1000u
#endif

/* Quadros entregues por DG_DrawFrame */
static uint32_t frames_drawn;

/* Contador de instrucoes no primeiro e no ultimo quadro, para instrucoes por quadro */
static uint64_t instret_first_frame;
static uint64_t instret_last_frame;

/* Le o CSR instret (64 bits) com a releitura do hi que o RV32 exige */
uint64_t doom_read_instret(void)
{
    uint32_t hi, lo, hi2;

    do
    {
        __asm__ volatile("rdinstreth %0" : "=r"(hi));
        __asm__ volatile("rdinstret %0" : "=r"(lo));
        __asm__ volatile("rdinstreth %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

#ifdef DOOM_USE_TIME_CSR
/* Le o CSR time (64 bits) com a releitura do hi que o RV32 exige */
static uint64_t read_time(void)
{
    uint32_t hi, lo, hi2;

    do
    {
        __asm__ volatile("rdtimeh %0" : "=r"(hi));
        __asm__ volatile("rdtime %0" : "=r"(lo));
        __asm__ volatile("rdtimeh %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}
#endif

void DG_Init(void)
{
    /* Spike: sem tela nem teclado para inicializar */
}

void DG_SleepMs(uint32_t ms)
{
    /* Espera ocupada pelo relogio; no -timedemo o jogo quase nao chama isto */
    uint32_t start = DG_GetTicksMs();

    while ((uint32_t)(DG_GetTicksMs() - start) < ms)
    {
    }
}

uint32_t DG_GetTicksMs(void)
{
#ifdef DOOM_USE_TIME_CSR
    return (uint32_t)(read_time() / DOOM_TIME_TICKS_PER_MS);
#else
    /* Relogio virtual do Spike: 100000 instrucoes por ms (Core de 100 MHz, 1 IPC) */
    return (uint32_t)(doom_read_instret() / 100000u);
#endif
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    /* Sem teclado: nenhum evento pendente */
    (void)pressed;
    (void)key;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

void DG_DrawFrame(void)
{
    if (frames_drawn == 0)
    {
        instret_first_frame = doom_read_instret();
    }
    instret_last_frame = doom_read_instret();
    frames_drawn++;
    /* Progresso a cada 100 quadros, para acompanhar a execucao longa no Spike */
    if (frames_drawn % 100u == 0)
    {
        printf("frame %lu instret %llu\n", (unsigned long)frames_drawn,
               (unsigned long long)instret_last_frame);
    }
}

uint32_t doom_frames_drawn(void)
{
    return frames_drawn;
}

uint64_t doom_instret_first_frame(void)
{
    return instret_first_frame;
}

uint64_t doom_instret_last_frame(void)
{
    return instret_last_frame;
}

/*
 * Ponto unico de conversao do framebuffer. Hoje devolve o CRC-32 (IEEE) dos
 * bytes do buffer 320x200 XRGB de 32 bits, que e o gabarito do D1. A placa
 * troca o corpo por uma conversao para o formato escolhido (DD2).
 */
uint32_t doom_frame_crc32(void)
{
    const uint8_t *p = (const uint8_t *)DG_ScreenBuffer;
    size_t n = (size_t)DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(pixel_t);
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < n; i++)
    {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
