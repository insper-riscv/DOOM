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

#endif
