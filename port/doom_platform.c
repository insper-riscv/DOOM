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

/* Le o CSR cycle (64 bits) com a releitura do hi que o RV32 exige */
uint64_t doom_read_cycle(void)
{
    uint32_t hi, lo, hi2;

    do
    {
        __asm__ volatile("rdcycleh %0" : "=r"(hi));
        __asm__ volatile("rdcycle %0" : "=r"(lo));
        __asm__ volatile("rdcycleh %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

#ifdef DOOM_MEASURE

/* Bloco de medicao na SDRAM (doom_platform.h), lido pelo dump-ram */
#define MEAS ((volatile uint32_t *)(uintptr_t)DOOM_MEASURE_ADDR)

#ifdef DOOM_MEASURE_HPM

/* Codigos de evento do Zihpm (Core/src/Zicsr/csr_event_consts.vhd), um por contador de indice 3 a 11:
 * 1 parada de mul/div, 2 espera por RAM externa, 3 desvio tomado, 4 acesso desalinhado,
 * 5 falha da cache de instrucoes, 6 falha da cache de dados, 7 espera por FLASH,
 * 8 parada por dependencia de load, 9 operacao fundida */
static const uint8_t meas_events[DOOM_MEAS_HPM] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

/* Escreve o seletor de evento mhpmeventN (CSR 0x320 + N) */
#define HPM_EVENT_WRITE(csr, code) __asm__ volatile("csrw " #csr ", %0" ::"r"(code))

/* Le o contador hpmcounterN (CSR 0xC00 + N), so os 32 bits baixos; k e o indice menos 3 */
static uint32_t meas_hpm_read(uint32_t k)
{
    uint32_t v = 0;

    switch (k)
    {
    case 0: __asm__ volatile("csrr %0, 0xC03" : "=r"(v)); break;
    case 1: __asm__ volatile("csrr %0, 0xC04" : "=r"(v)); break;
    case 2: __asm__ volatile("csrr %0, 0xC05" : "=r"(v)); break;
    case 3: __asm__ volatile("csrr %0, 0xC06" : "=r"(v)); break;
    case 4: __asm__ volatile("csrr %0, 0xC07" : "=r"(v)); break;
    case 5: __asm__ volatile("csrr %0, 0xC08" : "=r"(v)); break;
    case 6: __asm__ volatile("csrr %0, 0xC09" : "=r"(v)); break;
    case 7: __asm__ volatile("csrr %0, 0xC0A" : "=r"(v)); break;
    default: __asm__ volatile("csrr %0, 0xC0B" : "=r"(v)); break;
    }
    return v;
}

#endif /* DOOM_MEASURE_HPM */

void doom_measure_init(void)
{
    MEAS[DOOM_MEAS_H_MAGIC] = 0;
    MEAS[DOOM_MEAS_H_COUNT] = 0;
    MEAS[DOOM_MEAS_H_STEP] = DOOM_MEAS_STEP;
#ifdef DOOM_MEASURE_HPM
    MEAS[DOOM_MEAS_H_NHPM] = DOOM_MEAS_HPM;
    /* mcountinhibit = 0: todos os contadores contam; depois o evento de cada um */
    __asm__ volatile("csrw 0x320, zero");
    HPM_EVENT_WRITE(0x323, meas_events[0]);
    HPM_EVENT_WRITE(0x324, meas_events[1]);
    HPM_EVENT_WRITE(0x325, meas_events[2]);
    HPM_EVENT_WRITE(0x326, meas_events[3]);
    HPM_EVENT_WRITE(0x327, meas_events[4]);
    HPM_EVENT_WRITE(0x328, meas_events[5]);
    HPM_EVENT_WRITE(0x329, meas_events[6]);
    HPM_EVENT_WRITE(0x32A, meas_events[7]);
    HPM_EVENT_WRITE(0x32B, meas_events[8]);
    for (uint32_t k = 0; k < DOOM_MEAS_HPM; k++)
    {
        MEAS[DOOM_MEAS_H_EVENTS + k] = meas_events[k];
    }
#else
    MEAS[DOOM_MEAS_H_NHPM] = 0;
#endif
}

void doom_measure_snapshot(uint32_t frames)
{
    if (frames % DOOM_MEAS_STEP != 0)
    {
        return;
    }

    uint32_t idx = MEAS[DOOM_MEAS_H_COUNT];

    if (idx >= DOOM_MEAS_MAX)
    {
        return;
    }

    volatile uint32_t *s = MEAS + DOOM_MEAS_HEAD_WORDS + idx * DOOM_MEAS_SNAP_WORDS;
    uint64_t cyc = doom_read_cycle();
    uint64_t ins = doom_read_instret();

    s[DOOM_MEAS_S_FRAME] = frames;
    s[DOOM_MEAS_S_CYC_LO] = (uint32_t)cyc;
    s[DOOM_MEAS_S_CYC_HI] = (uint32_t)(cyc >> 32);
    s[DOOM_MEAS_S_INS_LO] = (uint32_t)ins;
    s[DOOM_MEAS_S_INS_HI] = (uint32_t)(ins >> 32);
#ifdef DOOM_MEASURE_HPM
    for (uint32_t k = 0; k < DOOM_MEAS_HPM; k++)
    {
        s[DOOM_MEAS_S_HPM + k] = meas_hpm_read(k);
    }
#endif
    __sync_synchronize();
    MEAS[DOOM_MEAS_H_COUNT] = idx + 1u;
    MEAS[DOOM_MEAS_H_MAGIC] = DOOM_MEAS_MAGIC;
}

#else

void doom_measure_init(void)
{
}

void doom_measure_snapshot(uint32_t frames)
{
    (void)frames;
}

#endif /* DOOM_MEASURE */

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
#ifdef DOOM_FB_ADDR
    /*
     * Framebuffer num endereco fixo da SDRAM (scripts/layout.sh): o `dump-ram` o
     * le sem precisar do ponteiro do malloc. A doomgeneric ja alocou o dela antes
     * de chamar DG_Init; aquela area fica sem uso.
     */
    DG_ScreenBuffer = (pixel_t *)(uintptr_t)DOOM_FB_ADDR;
#endif
}

void DG_SleepMs(uint32_t ms)
{
    /* Espera ocupada pelo relogio; no -timedemo o jogo quase nao chama isto */
    uint32_t start = DG_GetTicksMs();

    while ((uint32_t)(DG_GetTicksMs() - start) < ms)
    {
    }
}

#ifndef DOOM_PLATFORM_CLOCK
/*
 * Na placa (DOOM_PLATFORM_CLOCK) esta funcao vem de runtime/doom_clock.c da
 * plataforma doom, copiado pelo build; aqui fica so a dos alvos do Spike.
 */
uint32_t DG_GetTicksMs(void)
{
#ifdef DOOM_USE_TIME_CSR
    return (uint32_t)(read_time() / DOOM_TIME_TICKS_PER_MS);
#else
    /* Relogio virtual do Spike: 100000 instrucoes por ms (Core de 100 MHz, 1 IPC) */
    return (uint32_t)(doom_read_instret() / 100000u);
#endif
}
#endif

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
    /* Instantaneo dos contadores, antes de qualquer outro trabalho desta chamada */
    doom_measure_snapshot(frames_drawn + 1u);
#ifdef DOOM_MEASURE_STOP
    /* Janela pedida concluida: encerra (o _exit grava o resumo e fica repetindo-o) */
    if (frames_drawn + 1u == DOOM_MEASURE_STOP)
    {
        extern void _exit(int);
        _exit(0);
    }
#endif
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
