/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cartao microSD da DE0-CV (periferico SD nativo, 4 bits) para carregar as WADs sem a JTAG.
 *
 * Duas formas de uso, escolhidas no build (SD=load ou SD=demand no build.sh):
 *  - DOOM_SD_LOAD:   no inicio do main, doom_sd_load_image copia o payload do cartao para a SDRAM (o `wads.bin` no
 *                    endereco do cabecalho) e confere o CRC-32; se nao ha cartao ou imagem, o jogo segue pelo
 *                    caminho da JTAG (a WAD ja na SDRAM por `load-ram --wad`).
 *  - DOOM_SD_DEMAND: doom_sd_load_index le o indice do cartao (nomes, setores e tamanhos das WADs) e o backend
 *                    de WAD (doom_wad_mem.c) le os blocos do cartao a cada W_ReadLump, sem copiar nada para a SDRAM;
 *                    sem cartao, usa a tabela fixa de wad_table.h como hoje.
 *
 * Com SDFS=fatfs (DOOM_SD_FATFS) o cartao e um FAT32 comum lido pelo FatFs (port/doom_fatfs.c) e o formato abaixo
 * nao vale: doom_sd_load_index monta as tabelas olhando os arquivos do cartao.
 *
 * Formato do cartao cru (little endian, sem sistema de arquivos; scripts/sdwrite.py grava):
 *   setor H (padrao 2048): cabecalho de 512 bytes
 *      0 magia0 'DOOM' (0x4D4F4F44)   4 magia1 'SD01' (0x31304453)   8 versao u16 (1)   10 flags u16 (bit 0: payload carregavel)
 *     12 quantidade de WADs   16 setores do indice   20 setor do payload   24 tamanho do payload (bytes)
 *     28 endereco de destino na SDRAM   32 CRC-32 do payload   36 CRC-32 do indice   508 CRC-32 dos bytes 0 a 507
 *   setores H+1 ...: indice, 128 bytes por WAD (4 por setor):
 *      0 nome[16]   16 titulo[32]   48 descricao[48]   96 setor inicial   100 tamanho   104 CRC-32 da WAD
 *    108 tipo (0 IWAD, 1 PWAD)   109 mapa (Master Levels)   112 deslocamento no payload
 *   setor do payload (multiplo de 8): o `wads.bin` cru, cada WAD alinhada a 4 KiB, ou seja, a WAD i comeca no setor
 *   payload + deslocamento / 512.
 */

#ifndef DOOM_SD_H
#define DOOM_SD_H

#include <stddef.h>
#include <stdint.h>

#define DOOM_SD_SECTOR_BYTES 512u
#define DOOM_SD_HEADER_SECTOR 2048u
#define DOOM_SD_MAGIC0 0x4D4F4F44u
#define DOOM_SD_MAGIC1 0x31304453u
#define DOOM_SD_VERSION 1u
#define DOOM_SD_FLAG_PAYLOAD 1u
#define DOOM_SD_ENTRY_BYTES 128u
#define DOOM_SD_MAX_WADS 32

enum doom_sd_status
{
    DOOM_SD_OK = 0,
    DOOM_SD_NO_CARD = -1,     /* o cartao nao respondeu (sem cartao, ou nao e SD v2) */
    DOOM_SD_UNSUPPORTED = -2, /* cartao SDSC (endereco por byte): so SDHC e SDXC */
    DOOM_SD_IO = -3,          /* erro de comando, de CRC16 ou tempo esgotado depois das tentativas */
    DOOM_SD_BAD_HEADER = -4,  /* magia, versao ou CRC do cabecalho ou do indice errados */
    DOOM_SD_BAD_CRC = -5,     /* CRC-32 do payload diferente do cabecalho */
    DOOM_SD_MISMATCH = -6,    /* o cartao nao e a imagem que este build espera (endereco ou tamanho) */
    DOOM_SD_NO_FS = -7        /* modo FatFs: nao montou o FAT32 ou nao achou nenhuma WAD */
};

/* Contadores da sessao, para o console e para medir a espera por fase */
struct doom_sd_stats
{
    uint32_t blocks;      /* blocos lidos do cartao */
    uint32_t cache_hits;  /* leituras atendidas pela cache de 8 setores */
    uint32_t retries;     /* blocos relidos por erro */
    uint32_t read_calls;  /* chamadas de doom_sd_read_bytes */
    uint32_t wait_us;     /* tempo total dentro de leituras do cartao */
    uint32_t max_read_us; /* a leitura mais demorada de uma chamada */
};

/* Inicializa o cartao (CMD0, CMD8, ACMD41, CMD2, CMD3, CMD7, ACMD6) e deixa o SD_CLK em 25 MHz; DOOM_SD_OK ou erro */
int doom_sd_init(void);

/* Verdadeiro depois de um doom_sd_init bem sucedido */
int doom_sd_is_ready(void);

/* Capacidade do cartao em setores de 512 bytes, lida do CSD (CMD9) na inicializacao; 0 se nao foi possivel ler */
uint32_t doom_sd_sector_count(void);

/* Le um setor de 512 bytes para dst (alinhado a 4 bytes); com ate 3 tentativas */
int doom_sd_read_block(uint32_t sector, void *dst);

/* Le len bytes a partir do byte `offset` da regiao que comeca em base_sector (com uma cache de 8 setores) */
size_t doom_sd_read_bytes(uint32_t base_sector, uint32_t offset, void *dst, size_t len);

/*
 * Leitura de uma WAD pelo `handle` que a tabela guarda no campo addr de struct doom_wad_entry. Na forma crua
 * (sdcard_direct_mapped) o handle e o setor inicial da WAD e vale doom_sd_read_bytes; na forma FatFs (DOOM_SD_FATFS,
 * sdcard_FatFs) o handle e o indice da WAD na tabela e a leitura passa por f_lseek e f_read (port/doom_fatfs.c).
 */
#ifdef DOOM_SD_FATFS
size_t doom_wad_read(uint32_t handle, uint32_t offset, void *dst, size_t len);
#else
static inline size_t doom_wad_read(uint32_t handle, uint32_t offset, void *dst, size_t len)
{
    return doom_sd_read_bytes(handle, offset, dst, len);
}
#endif

#ifdef DOOM_SD_FATFS
/* Para doom_fatfs.c: relogio em microssegundos e contabilidade de uma leitura (read_calls, wait_us, max_read_us) */
uint32_t doom_sd_now_us(void);
void doom_sd_account(uint32_t elapsed_us);

/* Para doom_fatfs.c: marca que a tabela dinamica de WADs vem do cartao */
void doom_sd_set_on_card(int on_card);
#endif

/* Gancho de progresso: chamado depois de cada bloco lido do cartao, com o total de blocos lidos desde o boot (NULL desliga). */
extern void (*doom_sd_progress_cb)(uint32_t blocks);

/* Total de blocos de 512 bytes lidos do cartao desde o boot */
uint32_t doom_sd_blocks_read(void);

/* Modo DOOM_SD_LOAD: copia o payload para a SDRAM e confere o CRC-32; expect_addr e expect_size sao o que o build espera */
int doom_sd_load_image(uint32_t expect_addr, uint32_t expect_size);

/* Modo DOOM_SD_DEMAND: le o cabecalho e o indice e monta as tabelas dinamicas de WADs e do menu */
int doom_sd_load_index(void);

/* Copia a tabela fixa de wad_table.h para as tabelas dinamicas (sem cartao: WADs na SDRAM, como hoje) */
void doom_sd_use_static_table(void);

/* Verdadeiro se as WADs vem do cartao (as entradas tem o setor no lugar do endereco) */
int doom_sd_wads_on_card(void);

/* Contadores acumulados */
const struct doom_sd_stats *doom_sd_stats(void);

/* CRC-32 (IEEE, o do zlib) de buf, continuando de crc (0 no inicio) */
uint32_t doom_sd_crc32(uint32_t crc, const void *buf, size_t len);

/* Descricao curta de um codigo de erro */
const char *doom_sd_status_text(int status);

#endif
