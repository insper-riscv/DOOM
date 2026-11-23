/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Camada de disco do FatFs (diskio.h) sobre o driver do cartao SD nativo (port/doom_sd.c). Somente leitura: o
 * FatFs e compilado com FF_FS_READONLY=1, entao disk_write nao e chamada nem declarada pelo uso. Uma unidade (0).
 *
 * O FatFs entrega destinos de qualquer alinhamento (uma leitura de varios setores vai direto para o buffer do
 * chamador, que pode comecar em endereco impar); o driver grava palavras de 32 bits e pede destino alinhado a 4
 * bytes, entao o destino desalinhado passa por um buffer de um setor.
 */

#include <stdint.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "doom_sd.h"

/* Setor intermediario para destinos desalinhados */
static uint32_t bounce[DOOM_SD_SECTOR_BYTES / 4u];

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0u)
    {
        return STA_NOINIT;
    }
    return doom_sd_is_ready() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0u)
    {
        return STA_NOINIT;
    }
    /* O sd_boot ja inicializou o cartao: so repete o CMD0 e o resto se ainda nao estiver pronto */
    if (!doom_sd_is_ready() && doom_sd_init() != DOOM_SD_OK)
    {
        return STA_NOINIT;
    }
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0u || count == 0u)
    {
        return RES_PARERR;
    }
    if (!doom_sd_is_ready())
    {
        return RES_NOTRDY;
    }
    for (UINT i = 0; i < count; i++)
    {
        BYTE *dst = buff + (size_t)i * DOOM_SD_SECTOR_BYTES;
        int status;

        if (((uintptr_t)dst & 3u) == 0u)
        {
            status = doom_sd_read_block((uint32_t)sector + i, dst);
        }
        else
        {
            status = doom_sd_read_block((uint32_t)sector + i, bounce);
            if (status == DOOM_SD_OK)
            {
                memcpy(dst, bounce, DOOM_SD_SECTOR_BYTES);
            }
        }
        if (status != DOOM_SD_OK)
        {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0u)
    {
        return RES_PARERR;
    }
    switch (cmd)
    {
    case CTRL_SYNC:
        return RES_OK; /* somente leitura: nada pendente */
    case GET_SECTOR_COUNT:
        if (doom_sd_sector_count() == 0u)
        {
            return RES_ERROR;
        }
        *(LBA_t *)buff = doom_sd_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = DOOM_SD_SECTOR_BYTES;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1u; /* tamanho de bloco de apagamento desconhecido (so o f_mkfs usa) */
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
