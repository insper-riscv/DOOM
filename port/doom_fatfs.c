/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Forma sdcard_FatFs (SDFS=fatfs no build.sh): as WADs sao arquivos de um cartao FAT32 comum, lidos com o FatFs de
 * ChaN (R0.15a, somente leitura, port/ffconf.h) sobre o driver do cartao (port/doom_sd.c, port/doom_diskio.c).
 *
 * Em tempo de execucao este arquivo faz tres coisas:
 *  - doom_sd_load_index monta o volume e preenche as tabelas dinamicas de WADs e do menu (doom_wad_src.h) com o que
 *    existe no cartao: DOOM.WAD, DOOM2.WAD, PLUTONIA.WAD e TNT.WAD na raiz e as fases do Master Levels na pasta
 *    MASTER (ou MASTER/WADS). O campo addr de cada entrada guarda o indice da WAD na tabela (o handle de doom_wad_read);
 *  - o mapa fase para MAP do Master Levels vem do DOOM2.MAP do cartao (MASTER/DOOM2.MAP ou /DOOM2.MAP) e, sem ele, da
 *    tabela embutida abaixo (a do DOOM2.MAP do pacote Master Levels);
 *  - doom_wad_read le uma WAD com f_lseek e f_read. Cada WAD em uso tem um FIL e um mapa de clusters (CLMT, FF_USE_FASTSEEK)
 *    num conjunto de POOL_FILES arquivos abertos; faltando lugar, o menos recente e fechado e reaberto quando
 *    voltar a ser lido.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "doom_menu.h"
#include "doom_sd.h"
#include "doom_wad_src.h"

#ifndef DOOM_SD_DEMAND
#error "SDFS=fatfs pede SD=demand (as tabelas dinamicas do doom_wad_src.h)"
#endif

/* Arquivos abertos ao mesmo tempo: o menu le a primeira IWAD e o jogo abre a IWAD e a PWAD escolhidas */
#define POOL_FILES 4
/* Palavras do CLMT: 1 + 2 por fragmento do arquivo, ou seja ate 127 fragmentos; mais que isso cai para o seek pela FAT */
#define CLMT_DWORDS 256u
#define MAP_FILE_MAX 2048u
#define NAME_BYTES 16
#define PATH_BYTES 28

/* Jogos do menu, na ordem do menu: arquivo na raiz, titulo e descricao (os mesmos do scripts/wads.sh) */
static const struct
{
    const char *file;
    const char *title;
    const char *subtitle;
} iwads[] = {
    { "DOOM.WAD", "ULTIMATE DOOM", "4 EPISODES - 36 LEVELS" },
    { "DOOM2.WAD", "DOOM II", "HELL ON EARTH - 32 LEVELS" },
    { "PLUTONIA.WAD", "PLUTONIA", "THE PLUTONIA EXPERIMENT - 32 LEVELS" },
    { "TNT.WAD", "TNT: EVILUTION", "FINAL DOOM - 32 LEVELS" },
};
#define IWAD_COUNT ((int)(sizeof(iwads) / sizeof(iwads[0])))

/* Mapa em que cada fase do Master Levels entra no DOOM II (o DOOM2.MAP do pacote), se o cartao nao trouxer o arquivo */
static const struct
{
    const char *name;
    uint8_t map;
} default_map[] = {
    { "attack.wad", 1 },   { "blacktwr.wad", 25 }, { "bloodsea.wad", 7 }, { "canyon.wad", 1 },
    { "catwalk.wad", 1 },  { "combine.wad", 1 },   { "fistula.wad", 1 },  { "garrison.wad", 1 },
    { "geryon.wad", 8 },   { "manor.wad", 1 },     { "mephisto.wad", 7 }, { "minos.wad", 5 },
    { "nessus.wad", 7 },   { "paradox.wad", 1 },   { "subspace.wad", 1 }, { "subterra.wad", 1 },
    { "teeth.wad", 31 },   { "ttrap.wad", 1 },     { "vesperas.wad", 9 }, { "virgil.wad", 3 },
};
#define DEFAULT_MAP_COUNT ((int)(sizeof(default_map) / sizeof(default_map[0])))

static FATFS volume;
static char names[DOOM_SD_MAX_WADS][NAME_BYTES];
static char paths[DOOM_SD_MAX_WADS][PATH_BYTES];
static char titles[DOOM_SD_MAX_WADS][NAME_BYTES];
static char map_text[MAP_FILE_MAX + 1u];
static int map_loaded;

/* Um arquivo aberto do conjunto: o FIL, o mapa de clusters e a WAD (indice da tabela, -1 se livre) */
struct open_wad
{
    FIL fil;
    DWORD clmt[CLMT_DWORDS];
    int wad;
    uint32_t last_use;
};
static struct open_wad pool[POOL_FILES];
static uint32_t use_clock;

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Copia src para dst (cap bytes) em minusculas, sempre terminada em zero */
static void copy_lower(char *dst, const char *src, size_t cap)
{
    size_t i = 0;

    for (; i + 1u < cap && src[i] != '\0'; i++)
    {
        dst[i] = lower(src[i]);
    }
    dst[i] = '\0';
}

/* Termina em ".wad" (sem diferenciar maiusculas)? */
static int has_wad_suffix(const char *name)
{
    size_t n = strlen(name);

    return n > 4u && name[n - 4u] == '.' && lower(name[n - 3u]) == 'w' && lower(name[n - 2u]) == 'a' &&
           lower(name[n - 1u]) == 'd';
}

/* ------------------------------------------------------------------------------------------------------------ */
/* DOOM2.MAP                                                                                                     */
/* ------------------------------------------------------------------------------------------------------------ */

/* Le o DOOM2.MAP do cartao para map_text; 1 se achou */
static void load_map_file(void)
{
    static const char *const where[] = { "/MASTER/DOOM2.MAP", "/DOOM2.MAP", "/MASTER/WADS/DOOM2.MAP" };

    map_loaded = 0;
    for (size_t i = 0; i < sizeof(where) / sizeof(where[0]); i++)
    {
        FIL f;
        UINT got = 0;

        if (f_open(&f, where[i], FA_READ) != FR_OK)
        {
            continue;
        }
        if (f_read(&f, map_text, MAP_FILE_MAX, &got) == FR_OK && got > 0u)
        {
            map_text[got] = '\0';
            map_loaded = 1;
        }
        f_close(&f);
        if (map_loaded)
        {
            return;
        }
    }
}

/* Mapa em que a fase `name` (minusculas, com .wad) entra: do DOOM2.MAP ("nome.wad: MAPnn") ou da tabela embutida */
static int map_of(const char *name)
{
    size_t n = strlen(name);

    if (map_loaded)
    {
        const char *p = map_text;

        while (*p != '\0')
        {
            const char *eol = p;
            size_t k = 0;

            while (*eol != '\0' && *eol != '\n')
            {
                eol++;
            }
            while (k < n && p + k < eol && lower(p[k]) == name[k])
            {
                k++;
            }
            if (k == n && p + k < eol && p[k] == ':')
            {
                const char *q = p + k + 1;

                while (q < eol && !(lower(q[0]) == 'm' && q + 3 < eol && lower(q[1]) == 'a' && lower(q[2]) == 'p'))
                {
                    q++;
                }
                if (q < eol)
                {
                    int map = 0;

                    for (q += 3; q < eol && *q >= '0' && *q <= '9'; q++)
                    {
                        map = map * 10 + (*q - '0');
                    }
                    if (map > 0)
                    {
                        return map;
                    }
                }
            }
            p = (*eol == '\0') ? eol : eol + 1;
        }
    }
    for (int i = 0; i < DEFAULT_MAP_COUNT; i++)
    {
        if (strcmp(default_map[i].name, name) == 0)
        {
            return default_map[i].map;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Tabelas                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------ */

/* Acrescenta uma WAD a tabela dinamica; devolve o indice ou -1 se a tabela encheu */
static int add_wad(const char *lname, const char *path, uint32_t size)
{
    int i = doom_dyn.wad_count;

    if (i >= DOOM_SD_MAX_WADS)
    {
        return -1;
    }
    copy_lower(names[i], lname, NAME_BYTES);
    snprintf(paths[i], PATH_BYTES, "%s", path);
    doom_dyn_wad_table[i].name = names[i];
    doom_dyn_wad_table[i].addr = (uint32_t)i;
    doom_dyn_wad_table[i].size = size;
    doom_dyn.wad_count = i + 1;
    return i;
}

/* Fases da pasta `dir`, em ordem alfabetica (insercao); devolve quantas entraram */
static int scan_master_dir(const char *dir)
{
    DIR d;
    FILINFO fno;
    int added = 0;
    int first = doom_dyn.wad_count;

    if (f_opendir(&d, dir) != FR_OK)
    {
        return 0;
    }
    while (f_readdir(&d, &fno) == FR_OK && fno.fname[0] != '\0')
    {
        char path[PATH_BYTES];
        char lname[NAME_BYTES];
        int at;

        if ((fno.fattrib & (AM_DIR | AM_HID | AM_SYS)) != 0u || !has_wad_suffix(fno.fname))
        {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, fno.fname);
        copy_lower(lname, fno.fname, sizeof(lname));
        at = add_wad(lname, path, (uint32_t)fno.fsize);
        if (at < 0)
        {
            break;
        }
        /* Insercao ordenada pelo nome entre as fases ja lidas desta pasta */
        for (int j = at; j > first; j--)
        {
            if (strcmp(names[j], names[j - 1]) >= 0)
            {
                break;
            }
            {
                char tn[NAME_BYTES];
                char tp[PATH_BYTES];
                uint32_t ts = doom_dyn_wad_table[j].size;

                memcpy(tn, names[j], NAME_BYTES);
                memcpy(names[j], names[j - 1], NAME_BYTES);
                memcpy(names[j - 1], tn, NAME_BYTES);
                memcpy(tp, paths[j], PATH_BYTES);
                memcpy(paths[j], paths[j - 1], PATH_BYTES);
                memcpy(paths[j - 1], tp, PATH_BYTES);
                doom_dyn_wad_table[j].size = doom_dyn_wad_table[j - 1].size;
                doom_dyn_wad_table[j - 1].size = ts;
            }
        }
        added++;
    }
    f_closedir(&d);
    return added;
}

int doom_sd_load_index(void)
{
    FRESULT res = f_mount(&volume, "", 1);
    int games = 0;
    int masters = 0;
    int doom2 = -1;

    for (int i = 0; i < POOL_FILES; i++)
    {
        pool[i].wad = -1;
    }
    if (res != FR_OK)
    {
        printf("[FS] f_mount falhou (FRESULT %d): cartao sem FAT32 valido\n", (int)res);
        return DOOM_SD_NO_FS;
    }
    printf("[FS] FAT%u montado: cluster de %u setores\n", volume.fs_type == FS_FAT32 ? 32u : 16u, (unsigned)volume.csize);
    doom_dyn.wad_count = 0;
    for (int i = 0; i < IWAD_COUNT; i++)
    {
        FILINFO fno;
        char path[PATH_BYTES];
        int at;

        snprintf(path, sizeof(path), "/%s", iwads[i].file);
        if (f_stat(path, &fno) != FR_OK || (fno.fattrib & AM_DIR) != 0u || fno.fsize == 0u)
        {
            continue;
        }
        at = add_wad(iwads[i].file, path, (uint32_t)fno.fsize);
        if (at < 0)
        {
            break;
        }
        doom_dyn_games[games].title = iwads[i].title;
        doom_dyn_games[games].subtitle = iwads[i].subtitle;
        doom_dyn_games[games].iwad = at;
        games++;
        if (i == 1)
        {
            doom2 = at;
        }
    }
    if (doom2 >= 0)
    {
        int first = doom_dyn.wad_count;

        load_map_file();
        masters = scan_master_dir("/MASTER");
        if (masters == 0)
        {
            masters = scan_master_dir("/MASTER/WADS");
        }
        for (int m = 0; m < masters; m++)
        {
            int at = first + m;

            copy_lower(titles[m], names[at], NAME_BYTES);
            titles[m][strlen(titles[m]) - 4u] = '\0';
            for (char *c = titles[m]; *c != '\0'; c++)
            {
                if (*c >= 'a' && *c <= 'z')
                {
                    *c = (char)(*c - 'a' + 'A');
                }
            }
            doom_dyn_masters[m].title = titles[m];
            doom_dyn_masters[m].wad = at;
            doom_dyn_masters[m].map = map_of(names[at]);
        }
    }
    doom_dyn.master_index = -1;
    doom_dyn.master_iwad = -1;
    doom_dyn.master_count = 0;
    if (masters > 0 && doom2 >= 0)
    {
        doom_dyn_games[games].title = "MASTER LEVELS";
        doom_dyn_games[games].subtitle = "20 EXTRA LEVELS FOR DOOM II";
        doom_dyn_games[games].iwad = -1;
        doom_dyn.master_index = games;
        doom_dyn.master_iwad = doom2;
        doom_dyn.master_count = masters;
        games++;
    }
    doom_dyn.game_count = games;
    printf("[FS] %d jogos, %d fases do Master Levels (%s)\n", games, masters,
           map_loaded ? "mapa do DOOM2.MAP" : "mapa embutido");
    if (games == 0)
    {
        return DOOM_SD_NO_FS;
    }
    doom_sd_set_on_card(1);
    return DOOM_SD_OK;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Leitura                                                                                                       */
/* ------------------------------------------------------------------------------------------------------------ */

/* Devolve o arquivo aberto da WAD `wad` (abrindo, com o CLMT, e fechando o menos recente se for preciso); NULL se falhou */
static struct open_wad *acquire(int wad)
{
    struct open_wad *victim = &pool[0];

    for (int i = 0; i < POOL_FILES; i++)
    {
        if (pool[i].wad == wad)
        {
            pool[i].last_use = ++use_clock;
            return &pool[i];
        }
        if (pool[i].wad < 0 || (victim->wad >= 0 && pool[i].last_use < victim->last_use))
        {
            victim = &pool[i];
        }
    }
    if (victim->wad >= 0)
    {
        f_close(&victim->fil);
        victim->wad = -1;
    }
    if (f_open(&victim->fil, paths[wad], FA_READ) != FR_OK)
    {
        return NULL;
    }
#ifdef DOOM_FATFS_NOCLMT
    /* So para medir no cocotb: sem o mapa de clusters, o seek percorre a cadeia da FAT */
    victim->fil.cltbl = NULL;
    if (0)
#else
    victim->clmt[0] = CLMT_DWORDS;
    victim->fil.cltbl = victim->clmt;
    if (f_lseek(&victim->fil, CREATE_LINKMAP) != FR_OK)
#endif
    {
        /* Mais de 127 fragmentos (cartao fragmentado): segue sem o CLMT, com o seek pela cadeia da FAT */
        printf("[FS] %s: CLMT precisa de %u palavras, seek pela FAT\n", paths[wad], (unsigned)victim->clmt[0]);
        victim->fil.cltbl = NULL;
    }
    victim->wad = wad;
    victim->last_use = ++use_clock;
    return victim;
}

size_t doom_wad_read(uint32_t handle, uint32_t offset, void *dst, size_t len)
{
    struct open_wad *w;
    uint32_t t0 = doom_sd_now_us();
    UINT got = 0;
    FRESULT res;

    if (handle >= (uint32_t)doom_dyn.wad_count || len == 0u)
    {
        return 0;
    }
    w = acquire((int)handle);
    if (w == NULL)
    {
        return 0;
    }
    if (f_tell(&w->fil) != offset && f_lseek(&w->fil, offset) != FR_OK)
    {
        f_close(&w->fil);
        w->wad = -1;
        return 0;
    }
    res = f_read(&w->fil, dst, (UINT)len, &got);
    if (res != FR_OK)
    {
        /* Erro de leitura: o FatFs deixa o FIL com erro fatal, entao fecha para reabrir na proxima chamada */
        f_close(&w->fil);
        w->wad = -1;
        got = 0;
    }
    doom_sd_account((uint32_t)(doom_sd_now_us() - t0));
    return got;
}
