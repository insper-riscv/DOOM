/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * memcpy e memset por palavra.
 *
 * Substituem os da picolibc, que copiam byte a byte e gastam cerca de 5 instrucoes por byte; so o I_FinishUpdate copia
 * 64 KB por quadro. O resultado dos dois e o mesmo, com menos instrucoes: o nucleo e RV32IM sem C e sem acesso
 * desalinhado rapido, entao a copia por palavra so vale quando origem e destino estao alinhados.
 */
#include <stddef.h>
#include <stdint.h>

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0u) {
        uint32_t *dw = (uint32_t *)d;
        const uint32_t *sw = (const uint32_t *)s;
        while (n >= 32u) {
            uint32_t a0 = sw[0], a1 = sw[1], a2 = sw[2], a3 = sw[3];
            uint32_t a4 = sw[4], a5 = sw[5], a6 = sw[6], a7 = sw[7];
            dw[0] = a0; dw[1] = a1; dw[2] = a2; dw[3] = a3;
            dw[4] = a4; dw[5] = a5; dw[6] = a6; dw[7] = a7;
            dw += 8; sw += 8; n -= 32u;
        }
        while (n >= 4u) {
            *dw++ = *sw++; n -= 4u;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    uint32_t v = (uint8_t)c * 0x01010101u;

    while (n && ((uintptr_t)d & 3u)) {
        *d++ = (uint8_t)c; n--;
    }
    uint32_t *dw = (uint32_t *)d;
    while (n >= 16u) {
        dw[0] = v; dw[1] = v; dw[2] = v; dw[3] = v;
        dw += 4; n -= 16u;
    }
    while (n >= 4u) {
        *dw++ = v; n -= 4u;
    }
    d = (uint8_t *)dw;
    while (n--) {
        *d++ = (uint8_t)c;
    }
    return dst;
}
