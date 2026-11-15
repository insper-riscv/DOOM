/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Estatisticas da camada de plataforma, usadas pelo runtime ao encerrar.
 */

#ifndef DOOM_PLATFORM_H
#define DOOM_PLATFORM_H

#include <stdint.h>

/* Contador de instrucoes retiradas (CSR instret) */
uint64_t doom_read_instret(void);

/* Quadros entregues por DG_DrawFrame */
uint32_t doom_frames_drawn(void);

/* instret no primeiro DG_DrawFrame */
uint64_t doom_instret_first_frame(void);

/* instret no ultimo DG_DrawFrame */
uint64_t doom_instret_last_frame(void);

/* CRC-32 do ultimo framebuffer 320x200 XRGB */
uint32_t doom_frame_crc32(void);

#endif
