/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Logo do Insper em vermelho sangue, com gotas, no estilo do DOOM (bitmap gerado por scripts/gen_logo.py).
 *
 * Desenha no canto superior esquerdo do jogo (o FPS fica no direito) e do menu. Cada cor do bitmap vira a entrada da
 * paleta indexada do jogo mais proxima, escolhida por doom_logo_set_palette quando a paleta muda.
 */

#ifndef DOOM_LOGO_H
#define DOOM_LOGO_H

#include <stdint.h>

/* Escolhe as quatro cores do logo na paleta RGB444 de 256 entradas (cor = (r << 8) | (g << 4) | b) */
void doom_logo_set_palette(const uint16_t *palette12);

/* Desenha o logo no quadro de indices de `stride` bytes por linha, com o canto superior esquerdo em (x, y) */
void doom_logo_draw(uint8_t *fb, uint32_t stride, uint32_t x, uint32_t y);

#endif
