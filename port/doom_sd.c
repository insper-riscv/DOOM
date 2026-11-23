/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cartao microSD da DE0-CV pelo periferico SD nativo (Peripherals/src/SD/native), em C, por polling. Inicializa o
 * cartao (SDHC ou SDXC, blocos de 512 bytes, 4 linhas, SD_CLK de 25 MHz), le setores e implementa os dois usos
 * descritos em doom_sd.h: copia do payload para a SDRAM (DOOM_SD_LOAD) e leitura sob demanda das WADs
 * (DOOM_SD_DEMAND). Sem sistema de arquivos.
 *
 * O controlador faz um bloco por comando (buffer de 128 palavras), entao a leitura e um laco de CMD17 e a copia do
 * buffer para a memoria e feita pela CPU, palavra a palavra. DOOM_SD_HOST_TEST troca os registradores, o relogio e a
 * espera por ganchos, para rodar este mesmo arquivo contra o VHDL no cocotb (scripts/sd_test).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "doom_menu.h"
#include "doom_sd.h"

#ifdef DOOM_SD_DEMAND
/* A tabela fixa do build (WADs na SDRAM) fica guardada em constantes antes de doom_wad_src.h trocar os nomes */
#include "wad_table.h"
enum
{
    DOOM_WAD_COUNT_STATIC = DOOM_WAD_COUNT,
    DOOM_MENU_GAME_COUNT_STATIC = DOOM_MENU_GAME_COUNT,
    DOOM_MENU_MASTER_INDEX_STATIC = DOOM_MENU_MASTER_INDEX,
    DOOM_MENU_MASTER_IWAD_STATIC = DOOM_MENU_MASTER_IWAD,
    DOOM_MENU_MASTER_COUNT_STATIC = DOOM_MENU_MASTER_COUNT
};
static const struct doom_wad_entry *const doom_wad_table_static = doom_wad_table;
static const struct doom_menu_game *const doom_menu_games_static = doom_menu_games;
static const struct doom_menu_master *const doom_menu_masters_static = doom_menu_masters;
#include "doom_wad_src.h"
#endif

/* Registradores do periferico, em palavras (Peripherals/docs/pt-br/SD/sd_native.md, secao 4) */
enum
{
    R_ARG = 0,
    R_CMD = 1,
    R_RESP0 = 2,
    R_STATUS = 6,
    R_ERROR = 7,
    R_CONTROL = 8,
    R_DIVIDER = 9,
    R_TIMEOUT = 10,
    R_DATA = 11,
    R_PTR = 12
};

/* Bits de STATUS, CONTROL e CMD */
enum
{
    ST_CMD_BUSY = 1u << 0,
    ST_DATA_BUSY = 1u << 1,
    ST_CMD_DONE = 1u << 2,
    ST_DATA_DONE = 1u << 3,
    CTL_CLK_ENABLE = 1u << 0,
    CTL_WIDE_BUS = 1u << 1,
    CTL_ABORT = 1u << 8,
    RSP_NONE = 0u << 6,
    RSP_R1 = 1u << 6,
    RSP_R3 = 2u << 6,
    RSP_R2 = 3u << 6,
    CMD_WAIT_BUSY = 1u << 8,
    CMD_DATA_EN = 1u << 9,
    ERR_CMD_TIMEOUT = 1u << 0,
    ERR_ALL = 0x3FFu
};

/* Clock do nucleo (clocks.core do platform.yaml) e divisores: SD_CLK = clock / (2 * (divisor + 1)) */
#ifndef DOOM_SD_CLOCK_HZ
#define DOOM_SD_CLOCK_HZ 100000000u
#endif
#ifndef DOOM_SD_DIVIDER
#define DOOM_SD_DIVIDER 1u /* 25 MHz, o maximo do modo de velocidade padrao */
#endif
#define INIT_DIVIDER ((DOOM_SD_CLOCK_HZ + 799999u) / 800000u - 1u) /* 400 kHz ou menos */
#define BLOCK_WORDS 128u
#define READ_TRIES 3
#define CMD_TIMEOUT_US 200000u
#define DATA_TIMEOUT_US 500000u
#define READY_TIMEOUT_US 1200000u
#define CACHE_SECTORS 8u
#define NO_SECTOR 0xFFFFFFFFu

#ifdef DOOM_SD_HOST_TEST
uint32_t (*doom_sd_hw_read)(uint32_t reg);
void (*doom_sd_hw_write)(uint32_t reg, uint32_t value);
uint32_t (*doom_sd_hw_now_us)(void);
void (*doom_sd_hw_delay_us)(uint32_t us);
uint8_t *doom_sd_host_window;
uint32_t doom_sd_host_window_addr;

#define REG_RD(r) doom_sd_hw_read(r)
#define REG_WR(r, v) doom_sd_hw_write((r), (v))
#define NOW_US() doom_sd_hw_now_us()
#define DELAY_US(us) doom_sd_hw_delay_us(us)
#define DEST_PTR(addr) (doom_sd_host_window + ((addr) - doom_sd_host_window_addr))
#else
#include "memory_map.h"

/* O memory_map.h da plataforma doom so traz o SD depois que a plataforma o liga (platform.yaml, SD: base 0x80008000) */
#ifndef RV32_SD_BASE
#define RV32_SD_BASE 0x80008000u
#endif

#define REG_RD(r) (*(volatile uint32_t *)(RV32_SD_BASE + 4u * (r)))
#define REG_WR(r, v) (*(volatile uint32_t *)(RV32_SD_BASE + 4u * (r)) = (v))
/* mtime do CLINT conta a 1 MHz (interrupts.mtime_hz): a palavra baixa e o relogio em microssegundos */
#define NOW_US() (*(volatile uint32_t *)RV32_MTIME)
#define DELAY_US(us) delay_us(us)
#define DEST_PTR(addr) ((uint8_t *)(uintptr_t)(addr))

/* Palavras que dizem "a imagem deste cartao ja foi copiada" (magia, CRC-32 e tamanho), na ultima pagina da SDRAM */
#ifndef DOOM_SD_MARK_ADDR
#define DOOM_SD_MARK_ADDR 0x43BFF800u
#endif
#define DOOM_SD_MARK_MAGIC 0x444F4B31u
#define SDRAM_MARK ((volatile uint32_t *)(uintptr_t)DOOM_SD_MARK_ADDR)

/* Espera us microssegundos pelo mtime */
static void delay_us(uint32_t us)
{
    uint32_t start = NOW_US();

    while ((uint32_t)(NOW_US() - start) < us)
    {
    }
}
#endif

static struct doom_sd_stats stats;
static uint32_t card_rca;
#ifdef DOOM_SD_FATFS
static uint32_t card_sectors;
#endif
static int card_ready;
static uint32_t cache_tag[CACHE_SECTORS];
static uint32_t cache_data[CACHE_SECTORS][BLOCK_WORDS];
static uint32_t crc_table[256];
static int crc_ready;

/* ------------------------------------------------------------------------------------------------------------ */
/* CRC-32                                                                                                        */
/* ------------------------------------------------------------------------------------------------------------ */

/* Monta a tabela do CRC-32 IEEE (polinomio refletido 0xEDB88320) na primeira chamada */
static void crc_init(void)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        uint32_t c = i;

        for (int k = 0; k < 8; k++)
        {
            c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        }
        crc_table[i] = c;
    }
    crc_ready = 1;
}

uint32_t doom_sd_crc32(uint32_t crc, const void *buf, size_t len)
{
    const uint8_t *p = buf;

    if (!crc_ready)
    {
        crc_init();
    }
    crc = ~crc;
    while (len-- > 0)
    {
        crc = (crc >> 8) ^ crc_table[(crc ^ *p++) & 0xFFu];
    }
    return ~crc;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Comandos                                                                                                      */
/* ------------------------------------------------------------------------------------------------------------ */

/* Le o STATUS ate os bits de `mask` valerem 1; o relogio so e lido a cada 64 consultas. 0 se chegaram, -1 no prazo */
static int wait_status(uint32_t mask, uint32_t timeout_us)
{
    uint32_t start = NOW_US();
    uint32_t polls = 0;

    for (;;)
    {
        if ((REG_RD(R_STATUS) & mask) == mask)
        {
            return 0;
        }
        if ((++polls & 63u) == 0u && (uint32_t)(NOW_US() - start) > timeout_us)
        {
            return -1;
        }
    }
}

/* Interrompe o comando e a transferencia em andamento e limpa marcas e erros, mantendo o resto do CONTROL */
static void engine_abort(void)
{
    uint32_t control = REG_RD(R_CONTROL) & 0x1Fu;

    REG_WR(R_CONTROL, control | CTL_ABORT);
    REG_WR(R_ERROR, ERR_ALL);
    REG_WR(R_STATUS, ST_CMD_DONE | ST_DATA_DONE);
}

/* Le e limpa ERROR e as marcas de fim; devolve os erros */
static uint32_t take_errors(void)
{
    uint32_t errors = REG_RD(R_ERROR);

    REG_WR(R_ERROR, errors);
    REG_WR(R_STATUS, ST_CMD_DONE | ST_DATA_DONE);
    return errors;
}

/* Envia um comando e espera o fim. Devolve 0, DOOM_SD_NO_CARD (sem resposta) ou DOOM_SD_IO */
static int send_cmd(uint32_t index, uint32_t argument, uint32_t flags)
{
    uint32_t errors;

    REG_WR(R_ARG, argument);
    REG_WR(R_CMD, index | flags);
    if (wait_status(ST_CMD_DONE, CMD_TIMEOUT_US) != 0)
    {
        engine_abort();
        return DOOM_SD_IO;
    }
    errors = take_errors();
    if ((errors & ERR_CMD_TIMEOUT) != 0u)
    {
        return DOOM_SD_NO_CARD;
    }
    return errors == 0u ? 0 : DOOM_SD_IO;
}

/* CMD55 e depois o comando de aplicacao */
static int send_acmd(uint32_t index, uint32_t argument, uint32_t flags)
{
    int status = send_cmd(55, card_rca << 16, RSP_R1);

    return status != 0 ? status : send_cmd(index, argument, flags);
}

#ifdef DOOM_SD_FATFS
/*
 * CMD9 (SEND_CSD) no estado stand-by, antes do CMD7: guarda a capacidade em setores. Para o CSD versao 2 (SDHC e
 * SDXC), C_SIZE sao os bits 69 a 48, ou seja os bits 29 a 8 de RESP1, e a capacidade e (C_SIZE + 1) * 1024 setores.
 * Falha aqui nao impede o uso do cartao: a capacidade fica em 0 (desconhecida) e so o disk_ioctl do FatFs a repete.
 */
static void read_csd(void)
{
    card_sectors = 0;
    if (send_cmd(9, card_rca << 16, RSP_R2) == 0 && ((REG_RD(R_RESP0 + 3) >> 22) & 3u) == 1u)
    {
        card_sectors = (((REG_RD(R_RESP0 + 1) >> 8) & 0x3FFFFFu) + 1u) * 1024u;
    }
}

uint32_t doom_sd_sector_count(void)
{
    return card_sectors;
}

int doom_sd_is_ready(void)
{
    return card_ready;
}

uint32_t doom_sd_now_us(void)
{
    return NOW_US();
}

void doom_sd_account(uint32_t elapsed_us)
{
    stats.read_calls++;
    stats.wait_us += elapsed_us;
    if (elapsed_us > stats.max_read_us)
    {
        stats.max_read_us = elapsed_us;
    }
}
#endif /* DOOM_SD_FATFS (CSD e ganchos do doom_diskio.c) */

int doom_sd_init(void)
{
    uint32_t ocr = 0;
    uint32_t start;
    int status;

    card_ready = 0;
    card_rca = 0;
#ifdef DOOM_SD_FATFS
    card_sectors = 0;
#endif
    for (uint32_t i = 0; i < CACHE_SECTORS; i++)
    {
        cache_tag[i] = NO_SECTOR;
    }
    engine_abort();
    REG_WR(R_CONTROL, 0);
    REG_WR(R_DIVIDER, INIT_DIVIDER);
    /* Pelo menos 74 clocks com CMD em 1 e 1 ms antes do CMD0: 2 ms a 400 kHz sao 800 clocks */
    REG_WR(R_CONTROL, CTL_CLK_ENABLE);
    DELAY_US(2000u);
    REG_WR(R_CONTROL, 0);
    if (wait_status(0u, 1u) != 0 || send_cmd(0, 0, RSP_NONE) == DOOM_SD_IO)
    {
        return DOOM_SD_IO;
    }
    status = send_cmd(8, 0x1AAu, RSP_R1);
    if (status != 0)
    {
        return status == DOOM_SD_NO_CARD ? DOOM_SD_NO_CARD : DOOM_SD_IO;
    }
    if ((REG_RD(R_RESP0) & 0xFFFu) != 0x1AAu)
    {
        return DOOM_SD_UNSUPPORTED;
    }
    start = NOW_US();
    do
    {
        status = send_acmd(41, 0x40FF8000u, RSP_R3);
        if (status != 0)
        {
            return status;
        }
        ocr = REG_RD(R_RESP0);
        if ((ocr & 0x80000000u) == 0u)
        {
            DELAY_US(5000u);
        }
    } while ((ocr & 0x80000000u) == 0u && (uint32_t)(NOW_US() - start) < READY_TIMEOUT_US);
    if ((ocr & 0x80000000u) == 0u)
    {
        return DOOM_SD_IO;
    }
    if ((ocr & 0x40000000u) == 0u)
    {
        return DOOM_SD_UNSUPPORTED;
    }
    if ((status = send_cmd(2, 0, RSP_R2)) != 0 || (status = send_cmd(3, 0, RSP_R1)) != 0)
    {
        return status;
    }
    card_rca = REG_RD(R_RESP0) >> 16;
#ifdef DOOM_SD_FATFS
    read_csd(); /* so na forma FatFs: o build cru nao fala CMD9 e fica identico */
#endif
    status = send_cmd(7, card_rca << 16, RSP_R1 | CMD_WAIT_BUSY);
    if (status == 0)
    {
        status = send_acmd(6, 2u, RSP_R1);
    }
    if (status != 0)
    {
        return status;
    }
    REG_WR(R_CONTROL, CTL_WIDE_BUS);
    REG_WR(R_DIVIDER, DOOM_SD_DIVIDER);
    REG_WR(R_TIMEOUT, 5000000u); /* 0,2 s a 25 MHz */
    card_ready = 1;
    return DOOM_SD_OK;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Blocos                                                                                                        */
/* ------------------------------------------------------------------------------------------------------------ */

/* Uma tentativa de CMD17 do setor, com a copia das 128 palavras do buffer do periferico para dst */
static int read_block_once(uint32_t sector, uint32_t *dst)
{
    REG_WR(R_ARG, sector);
    REG_WR(R_CMD, 17u | RSP_R1 | CMD_DATA_EN);
    if (wait_status(ST_CMD_DONE | ST_DATA_DONE, DATA_TIMEOUT_US) != 0)
    {
        engine_abort();
        return DOOM_SD_IO;
    }
    if (take_errors() != 0u)
    {
        return DOOM_SD_IO;
    }
    REG_WR(R_PTR, 0);
    for (uint32_t i = 0; i < BLOCK_WORDS; i += 4u)
    {
        uint32_t a = REG_RD(R_DATA);
        uint32_t b = REG_RD(R_DATA);
        uint32_t c = REG_RD(R_DATA);
        uint32_t d = REG_RD(R_DATA);

        dst[i] = a;
        dst[i + 1u] = b;
        dst[i + 2u] = c;
        dst[i + 3u] = d;
    }
    return DOOM_SD_OK;
}

void (*doom_sd_progress_cb)(uint32_t blocks);

uint32_t doom_sd_blocks_read(void)
{
    return stats.blocks;
}

int doom_sd_read_block(uint32_t sector, void *dst)
{
    int status = DOOM_SD_IO;

    if (!card_ready)
    {
        return DOOM_SD_NO_CARD;
    }
    for (int attempt = 0; attempt < READ_TRIES; attempt++)
    {
        status = read_block_once(sector, dst);
        if (status == DOOM_SD_OK)
        {
            stats.blocks++;
            if (doom_sd_progress_cb != NULL)
            {
                doom_sd_progress_cb(stats.blocks);
            }
            return status;
        }
        stats.retries++;
    }
    return status;
}

size_t doom_sd_read_bytes(uint32_t base_sector, uint32_t offset, void *dst, size_t len)
{
    uint8_t *out = dst;
    size_t done = 0;
    uint32_t sector = base_sector + offset / DOOM_SD_SECTOR_BYTES;
    uint32_t skip = offset % DOOM_SD_SECTOR_BYTES;
    uint32_t t0 = NOW_US();
    uint32_t elapsed;

    stats.read_calls++;
    while (done < len)
    {
        size_t chunk = DOOM_SD_SECTOR_BYTES - skip;

        if (chunk > len - done)
        {
            chunk = len - done;
        }
        if (chunk == DOOM_SD_SECTOR_BYTES && ((uintptr_t)out & 3u) == 0u)
        {
            /* Setor inteiro em endereco alinhado: direto para o destino, sem passar pela cache */
            if (doom_sd_read_block(sector, out) != DOOM_SD_OK)
            {
                break;
            }
        }
        else
        {
            uint32_t slot = sector & (CACHE_SECTORS - 1u);

            if (cache_tag[slot] == sector)
            {
                stats.cache_hits++;
            }
            else
            {
                cache_tag[slot] = NO_SECTOR;
                if (doom_sd_read_block(sector, cache_data[slot]) != DOOM_SD_OK)
                {
                    break;
                }
                cache_tag[slot] = sector;
            }
            memcpy(out, (const uint8_t *)cache_data[slot] + skip, chunk);
        }
        out += chunk;
        done += chunk;
        skip = 0;
        sector++;
    }
    elapsed = (uint32_t)(NOW_US() - t0);
    stats.wait_us += elapsed;
    if (elapsed > stats.max_read_us)
    {
        stats.max_read_us = elapsed;
    }
    return done;
}

const struct doom_sd_stats *doom_sd_stats(void)
{
    return &stats;
}

const char *doom_sd_status_text(int status)
{
    switch (status)
    {
    case DOOM_SD_OK: return "ok";
    case DOOM_SD_NO_CARD: return "sem cartao";
    case DOOM_SD_UNSUPPORTED: return "cartao nao suportado (so SDHC e SDXC)";
    case DOOM_SD_IO: return "erro de leitura";
    case DOOM_SD_BAD_HEADER: return "cabecalho ou indice invalido";
    case DOOM_SD_BAD_CRC: return "CRC-32 do payload nao confere";
    case DOOM_SD_MISMATCH: return "imagem do cartao diferente da do build";
#ifdef DOOM_SD_FATFS
    case DOOM_SD_NO_FS: return "cartao sem FAT32 ou sem WADs";
#endif
    default: return "erro desconhecido";
    }
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Cabecalho e indice                                                                                            */
/* ------------------------------------------------------------------------------------------------------------ */

/* Campos do cabecalho, ja lidos */
struct header
{
    uint16_t flags;
    uint32_t count;
    uint32_t index_sectors;
    uint32_t payload_sector;
    uint32_t payload_size;
    uint32_t dest_addr;
    uint32_t payload_crc;
    uint32_t index_crc;
};

/* Le um inteiro de 32 bits em little endian de qualquer endereco */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Le e confere o cabecalho do setor DOOM_SD_HEADER_SECTOR */
static int read_header(struct header *h)
{
    static uint32_t words[BLOCK_WORDS];
    const uint8_t *b = (const uint8_t *)words;
    int status = doom_sd_read_block(DOOM_SD_HEADER_SECTOR, words);

    if (status != DOOM_SD_OK)
    {
        return status;
    }
    if (le32(b) != DOOM_SD_MAGIC0 || le32(b + 4) != DOOM_SD_MAGIC1 ||
        (uint16_t)(b[8] | (b[9] << 8)) != DOOM_SD_VERSION ||
        doom_sd_crc32(0, b, 508) != le32(b + 508))
    {
        return DOOM_SD_BAD_HEADER;
    }
    h->flags = (uint16_t)(b[10] | (b[11] << 8));
    h->count = le32(b + 12);
    h->index_sectors = le32(b + 16);
    h->payload_sector = le32(b + 20);
    h->payload_size = le32(b + 24);
    h->dest_addr = le32(b + 28);
    h->payload_crc = le32(b + 32);
    h->index_crc = le32(b + 36);
    return DOOM_SD_OK;
}

int doom_sd_load_image(uint32_t expect_addr, uint32_t expect_size)
{
    struct header h;
    uint8_t *dest;
    uint32_t crc = 0;
    uint32_t full = 0;
    uint32_t next_report = 4u << 20;
    int status = read_header(&h);

    if (status != DOOM_SD_OK)
    {
        return status;
    }
    if ((h.flags & DOOM_SD_FLAG_PAYLOAD) == 0u || h.dest_addr != expect_addr || h.payload_size != expect_size)
    {
        return DOOM_SD_MISMATCH;
    }
#ifndef DOOM_SD_HOST_TEST
    /* Reinicio do jogo (Quit Game, fim por erro): a imagem copiada antes continua na SDRAM, nao copia de novo */
    if (SDRAM_MARK[0] == DOOM_SD_MARK_MAGIC && SDRAM_MARK[1] == h.payload_crc && SDRAM_MARK[2] == h.payload_size)
    {
        return DOOM_SD_OK;
    }
#endif
    dest = DEST_PTR(h.dest_addr);
    full = h.payload_size / DOOM_SD_SECTOR_BYTES;
    for (uint32_t s = 0; s < full; s++)
    {
        uint8_t *at = dest + (size_t)s * DOOM_SD_SECTOR_BYTES;

        status = doom_sd_read_block(h.payload_sector + s, at);
        if (status != DOOM_SD_OK)
        {
            return status;
        }
        crc = doom_sd_crc32(crc, at, DOOM_SD_SECTOR_BYTES);
        if ((s + 1u) * DOOM_SD_SECTOR_BYTES >= next_report)
        {
            printf("[SD] %u KiB de %u KiB\n", (unsigned)(next_report >> 10), (unsigned)(h.payload_size >> 10));
            next_report += 4u << 20;
        }
    }
    if (h.payload_size % DOOM_SD_SECTOR_BYTES != 0u)
    {
        size_t rest = h.payload_size % DOOM_SD_SECTOR_BYTES;
        uint8_t *at = dest + (size_t)full * DOOM_SD_SECTOR_BYTES;

        if (doom_sd_read_bytes(h.payload_sector + full, 0, at, rest) != rest)
        {
            return DOOM_SD_IO;
        }
        crc = doom_sd_crc32(crc, at, rest);
    }
    if (crc != h.payload_crc)
    {
        return DOOM_SD_BAD_CRC;
    }
#ifndef DOOM_SD_HOST_TEST
    SDRAM_MARK[0] = DOOM_SD_MARK_MAGIC;
    SDRAM_MARK[1] = h.payload_crc;
    SDRAM_MARK[2] = h.payload_size;
#endif
    return DOOM_SD_OK;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Tabelas dinamicas (DOOM_SD_DEMAND)                                                                            */
/* ------------------------------------------------------------------------------------------------------------ */

#ifdef DOOM_SD_DEMAND

struct doom_dyn_tables doom_dyn;
struct doom_wad_entry doom_dyn_wad_table[DOOM_SD_MAX_WADS];
struct doom_menu_game doom_dyn_games[DOOM_SD_MAX_WADS + 1];
struct doom_menu_master doom_dyn_masters[DOOM_SD_MAX_WADS];

static int wads_on_card;
#ifndef DOOM_SD_FATFS
static char names[DOOM_SD_MAX_WADS][16];
static char titles[DOOM_SD_MAX_WADS][32];
static char subtitles[DOOM_SD_MAX_WADS][48];
static uint8_t index_buf[DOOM_SD_MAX_WADS * DOOM_SD_ENTRY_BYTES] __attribute__((aligned(4)));
#endif

int doom_sd_wads_on_card(void)
{
    return wads_on_card;
}

#ifndef DOOM_SD_FATFS
/* Copia uma cadeia de ate cap-1 caracteres, sempre terminada em zero */
static void copy_text(char *dst, const uint8_t *src, size_t cap)
{
    size_t i = 0;

    for (; i + 1u < cap && src[i] != 0u; i++)
    {
        dst[i] = (char)src[i];
    }
    dst[i] = '\0';
}
#endif

void doom_sd_use_static_table(void)
{
    wads_on_card = 0;
    doom_dyn.wad_count = DOOM_WAD_COUNT_STATIC;
    for (int i = 0; i < DOOM_WAD_COUNT_STATIC; i++)
    {
        doom_dyn_wad_table[i] = doom_wad_table_static[i];
    }
    doom_dyn.game_count = DOOM_MENU_GAME_COUNT_STATIC;
    for (int i = 0; i < DOOM_MENU_GAME_COUNT_STATIC; i++)
    {
        doom_dyn_games[i] = doom_menu_games_static[i];
    }
    doom_dyn.master_index = DOOM_MENU_MASTER_INDEX_STATIC;
    doom_dyn.master_iwad = DOOM_MENU_MASTER_IWAD_STATIC;
    doom_dyn.master_count = DOOM_MENU_MASTER_COUNT_STATIC;
    for (int i = 0; i < DOOM_MENU_MASTER_COUNT_STATIC; i++)
    {
        doom_dyn_masters[i] = doom_menu_masters_static[i];
    }
}

#ifndef DOOM_SD_FATFS
int doom_sd_load_index(void)
{
    struct header h;
    int status = read_header(&h);
    int games = 0;
    int masters = 0;
    int doom2 = -1;

    if (status != DOOM_SD_OK)
    {
        return status;
    }
    if (h.count == 0u || h.count > DOOM_SD_MAX_WADS || h.index_sectors != (h.count + 3u) / 4u)
    {
        return DOOM_SD_BAD_HEADER;
    }
    for (uint32_t s = 0; s < h.index_sectors; s++)
    {
        static uint32_t sector_words[BLOCK_WORDS];

        status = doom_sd_read_block(DOOM_SD_HEADER_SECTOR + 1u + s, sector_words);
        if (status != DOOM_SD_OK)
        {
            return status;
        }
        memcpy(index_buf + (size_t)s * DOOM_SD_SECTOR_BYTES, sector_words, DOOM_SD_SECTOR_BYTES);
    }
    /* O CRC-32 do indice cobre as entradas, preenchidas com zero ate o fim do ultimo setor */
    {
        uint32_t crc = doom_sd_crc32(0, index_buf, (size_t)h.index_sectors * DOOM_SD_SECTOR_BYTES);

        if (crc != h.index_crc)
        {
            return DOOM_SD_BAD_HEADER;
        }
    }
    for (uint32_t i = 0; i < h.count; i++)
    {
        const uint8_t *e = index_buf + (size_t)i * DOOM_SD_ENTRY_BYTES;

        copy_text(names[i], e, sizeof(names[i]));
        copy_text(titles[i], e + 16, sizeof(titles[i]));
        copy_text(subtitles[i], e + 48, sizeof(subtitles[i]));
        doom_dyn_wad_table[i].name = names[i];
        doom_dyn_wad_table[i].addr = le32(e + 96);
        doom_dyn_wad_table[i].size = le32(e + 100);
        if (e[108] == 0u)
        {
            doom_dyn_games[games].title = titles[i];
            doom_dyn_games[games].subtitle = subtitles[i];
            doom_dyn_games[games].iwad = (int)i;
            games++;
            if (strcmp(names[i], "doom2.wad") == 0)
            {
                doom2 = (int)i;
            }
        }
        else
        {
            doom_dyn_masters[masters].title = titles[i];
            doom_dyn_masters[masters].wad = (int)i;
            doom_dyn_masters[masters].map = e[109];
            masters++;
        }
    }
    doom_dyn.master_index = -1;
    doom_dyn.master_iwad = -1;
    if (masters > 0 && doom2 >= 0)
    {
        doom_dyn_games[games].title = "MASTER LEVELS";
        doom_dyn_games[games].subtitle = "20 EXTRA LEVELS FOR DOOM II";
        doom_dyn_games[games].iwad = -1;
        doom_dyn.master_index = games;
        doom_dyn.master_iwad = doom2;
        games++;
    }
    if (games == 0)
    {
        return DOOM_SD_BAD_HEADER;
    }
    doom_dyn.wad_count = (int)h.count;
    doom_dyn.game_count = games;
    doom_dyn.master_count = doom2 >= 0 ? masters : 0;
    wads_on_card = 1;
    return DOOM_SD_OK;
}

#endif /* DOOM_SD_FATFS */

#ifdef DOOM_SD_FATFS
void doom_sd_set_on_card(int on_card)
{
    wads_on_card = on_card;
}
#endif

#endif /* DOOM_SD_DEMAND */
