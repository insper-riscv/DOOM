/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Logo do Insper em vermelho sangue (ver doom_logo.h).
 */

#include <stdint.h>

#include "doom_logo.h"
#include "doom_logo_data.h"

/* Cores alvo, em RGB444, de cada valor do bitmap: 1 contorno, 2 sangue escuro, 3 sangue, 4 brilho */
static const uint16_t target12[5] = {0x000u, 0x300u, 0x800u, 0xC00u, 0xF30u};

/* Entrada da paleta do jogo para cada valor do bitmap (o 0 e transparente) */
static uint8_t color_index[5];

static uint32_t distance(uint16_t a, uint16_t b)
{
    int32_t dr = (int32_t)(a >> 8) - (int32_t)(b >> 8);
    int32_t dg = (int32_t)((a >> 4) & 0xFu) - (int32_t)((b >> 4) & 0xFu);
    int32_t db = (int32_t)(a & 0xFu) - (int32_t)(b & 0xFu);

    return (uint32_t)(dr * dr + dg * dg + db * db);
}

void doom_logo_set_palette(const uint16_t *palette12)
{
    static int fixed;

    /* So a primeira paleta (a normal do jogo) escolhe as cores: o DOOM tinge a tela com a mesma paleta de indices quando
     * leva dano ou pega item, e o logo acompanha a cena em vez de trocar de indice a cada tingimento */
    if (fixed != 0)
    {
        return;
    }
    fixed = 1;
    for (uint32_t v = 1; v < 5u; v++)
    {
        uint32_t best = 0xFFFFFFFFu;

        for (uint32_t i = 0; i < 256u; i++)
        {
            uint32_t d = distance(palette12[i], target12[v]);

            if (d < best)
            {
                best = d;
                color_index[v] = (uint8_t)i;
            }
        }
    }
}

void doom_logo_draw(uint8_t *fb, uint32_t stride, uint32_t x, uint32_t y)
{
    const uint8_t *bits = doom_logo_bits;

    for (uint32_t row = 0; row < DOOM_LOGO_PX_H; row++)
    {
        uint8_t *dst = fb + (y + row) * stride + x;

        for (uint32_t col = 0; col < DOOM_LOGO_PX_W; col++)
        {
            uint8_t v = bits[col];

            if (v != 0u)
            {
                dst[col] = color_index[v];
            }
        }
        bits += DOOM_LOGO_PX_W;
    }
}
