/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Estatisticas da camada de plataforma, usadas pelo runtime ao encerrar, e o
 * layout do bloco de resultado que o hospedeiro le com o `dump-ram`.
 */

#ifndef DOOM_PLATFORM_H
#define DOOM_PLATFORM_H

#include <stdint.h>

/* Contador de instrucoes retiradas (CSR instret) */
uint64_t doom_read_instret(void);

/* Contador de ciclos (CSR cycle) */
uint64_t doom_read_cycle(void);

/* Quadros entregues por DG_DrawFrame */
uint32_t doom_frames_drawn(void);

/* instret no primeiro DG_DrawFrame */
uint64_t doom_instret_first_frame(void);

/* instret no ultimo DG_DrawFrame */
uint64_t doom_instret_last_frame(void);

/* CRC-32 do ultimo framebuffer 320x200 XRGB */
uint32_t doom_frame_crc32(void);

/*
 * Bloco de resultado: 16 palavras de 32 bits a partir de DOOM_RESULT_ADDR (-D do
 * build, scripts/layout.sh), na ultima pagina da SDRAM. O jogo zera a magia ao
 * iniciar e a grava por ultimo ao terminar o timedemo; o hospedeiro so confia nos
 * campos quando a magia vale DOOM_RESULT_MAGIC.
 */
#define DOOM_RESULT_MAGIC 0x53455244u /* bytes "DRES" */

enum doom_result_word
{
    DOOM_RES_MAGIC = 0,    /* DOOM_RESULT_MAGIC quando o bloco esta completo */
    DOOM_RES_EXIT = 1,     /* codigo de saida do jogo (-1 no fim do timedemo) */
    DOOM_RES_CRC32 = 2,    /* CRC-32 (IEEE) do framebuffer 320x200 XRGB de 32 bits */
    DOOM_RES_FRAMES = 3,   /* quadros entregues */
    DOOM_RES_INSTRET_LO = 4,
    DOOM_RES_INSTRET_HI = 5,
    DOOM_RES_PER_FRAME = 6, /* instrucoes por quadro (media) */
    DOOM_RES_FB_ADDR = 7,  /* endereco do framebuffer */
    DOOM_RES_MS = 8,       /* milissegundos do DG_GetTicksMs ao terminar */
    DOOM_RES_CYCLE_LO = 9,
    DOOM_RES_CYCLE_HI = 10,
    DOOM_RES_WORDS = 16
};

/*
 * Medicao de CPI por janelas de quadros (DOOM_MEASURE, desligado por padrao). A cada
 * DOOM_MEAS_STEP quadros entregues, DG_DrawFrame grava um instantaneo dos contadores
 * (cycle, instret e, na placa com Zihpm, os nove contadores de eventos) no bloco que
 * comeca em DOOM_MEASURE_ADDR (-D do build, scripts/layout.sh), na ultima pagina da SDRAM.
 * O hospedeiro le o bloco com o `dump-ram` e scripts/measure_report.py calcula os deltas
 * entre instantaneos consecutivos. O cabecalho tem 16 palavras; cada instantaneo, 16.
 *
 *   cabecalho: magia, quantidade de instantaneos, passo (quadros), numero de contadores
 *              de evento, e os nove codigos de evento configurados (palavras 4 a 12)
 *   instantaneo: quadro, cycle lo, cycle hi, instret lo, instret hi, nove contadores
 *              de evento (32 bits baixos), uma palavra de enchimento
 */
#define DOOM_MEAS_MAGIC 0x5341454Du /* bytes "MEAS" */
#define DOOM_MEAS_STEP 100u         /* quadros entre instantaneos */
#define DOOM_MEAS_MAX 64u           /* instantaneos que cabem no bloco */
#define DOOM_MEAS_HPM 9u            /* contadores de evento lidos (indices 3 a 11) */
#define DOOM_MEAS_SNAP_WORDS 16u
#define DOOM_MEAS_HEAD_WORDS 16u
#define DOOM_MEAS_FRAME_OFF 2048u   /* palavra do bloco onde comecam os ciclos de cada quadro */
#define DOOM_MEAS_FRAME_MAX 1024u   /* quadros registrados, um por palavra (32 bits baixos do delta) */

enum doom_meas_head_word
{
    DOOM_MEAS_H_MAGIC = 0,
    DOOM_MEAS_H_COUNT = 1,
    DOOM_MEAS_H_STEP = 2,
    DOOM_MEAS_H_NHPM = 3,
    DOOM_MEAS_H_EVENTS = 4 /* nove palavras, de 4 a 12 */
};

enum doom_meas_snap_word
{
    DOOM_MEAS_S_FRAME = 0,
    DOOM_MEAS_S_CYC_LO = 1,
    DOOM_MEAS_S_CYC_HI = 2,
    DOOM_MEAS_S_INS_LO = 3,
    DOOM_MEAS_S_INS_HI = 4,
    DOOM_MEAS_S_HPM = 5 /* nove palavras, de 5 a 13 */
};

/* Configura os contadores de evento e zera mcountinhibit (no-op sem DOOM_MEASURE_HPM) */
void doom_measure_init(void);

/* Grava um instantaneo se o quadro entregue e multiplo do passo (no-op sem DOOM_MEASURE) */
void doom_measure_snapshot(uint32_t frames_drawn);

#endif
