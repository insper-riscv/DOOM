/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Backend de WAD em memoria para a doomgeneric.
 *
 * Substitui o w_file_stdc.c da doomgeneric: define a classe `stdc_wad_file`
 * (o nome que o w_file.c procura) lendo a WAD com memcpy de uma imagem que ja
 * esta na RAM. Nao usa open, read nem lseek, que a picolibc nao fornece.
 *
 * A origem da imagem tem duas formas:
 *  - build do Spike: a WAD e embutida no ELF (objcopy -I binary) e os
 *    simbolos _binary_doom1_wad_start e _binary_doom1_wad_end a delimitam;
 *  - placa: DOOM_WAD_ADDR e DOOM_WAD_SIZE (-D) apontam para a regiao da SDRAM
 *    onde a JTAG deixou a WAD.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "doomtype.h"
#include "w_file.h"
#include "z_zone.h"

/* Nome que o jogo usa para a WAD (-iwad); qualquer outro caminho nao existe */
#define DOOM_WAD_NAME "doom1.wad"

#ifdef DOOM_WAD_ADDR
/* Imagem da WAD num endereco fixo da SDRAM, definido por -D no build da placa */
static const uint8_t *wad_image(void) { return (const uint8_t *)(uintptr_t)DOOM_WAD_ADDR; }
static size_t wad_image_size(void) { return (size_t)(DOOM_WAD_SIZE); }
#else
extern const uint8_t _binary_doom1_wad_start[];
extern const uint8_t _binary_doom1_wad_end[];

/* Imagem da WAD embutida no ELF */
static const uint8_t *wad_image(void) { return _binary_doom1_wad_start; }

/* Tamanho da WAD embutida no ELF */
static size_t wad_image_size(void)
{
    return (size_t)(_binary_doom1_wad_end - _binary_doom1_wad_start);
}
#endif

/* Informa ao w_wad.c se o caminho e a WAD em memoria (substitui o fopen do M_FileExists) */
boolean __wrap_M_FileExists(char *filename)
{
    return strcmp(filename, DOOM_WAD_NAME) == 0;
}

extern wad_file_class_t stdc_wad_file;

/* Unica WAD aberta: a estrutura vive no heap da zona, sem arquivo associado */
static wad_file_t *mem_wad_open(char *path)
{
    wad_file_t *result;

    if (strcmp(path, DOOM_WAD_NAME) != 0)
    {
        return NULL;
    }

    result = Z_Malloc(sizeof(wad_file_t), PU_STATIC, 0);
    result->file_class = &stdc_wad_file;
    result->mapped = NULL;
    result->length = (unsigned int)wad_image_size();
    return result;
}

/* Fecha a WAD: nada a liberar alem da estrutura */
static void mem_wad_close(wad_file_t *wad)
{
    Z_Free(wad);
}

/* Copia buffer_len bytes a partir de offset, truncando no fim da imagem */
static size_t mem_wad_read(wad_file_t *wad, unsigned int offset,
                           void *buffer, size_t buffer_len)
{
    size_t size = wad_image_size();

    (void)wad;
    if (offset >= size)
    {
        return 0;
    }
    if (buffer_len > size - offset)
    {
        buffer_len = size - offset;
    }
    memcpy(buffer, wad_image() + offset, buffer_len);
    return buffer_len;
}

wad_file_class_t stdc_wad_file =
{
    mem_wad_open,
    mem_wad_close,
    mem_wad_read,
};
