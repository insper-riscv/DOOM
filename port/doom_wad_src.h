/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tabelas de WADs dinamicas do modo DOOM_SD_DEMAND. Incluir DEPOIS de wad_table.h: no modo sob demanda as tabelas e
 * os contadores que o menu, o main e o backend de WAD usam passam a ser os que doom_sd.c preenche em tempo de
 * execucao (do indice do cartao ou, sem cartao, da copia da tabela fixa). Sem DOOM_SD_DEMAND nao faz nada.
 */

#ifndef DOOM_WAD_SRC_H
#define DOOM_WAD_SRC_H

#ifdef DOOM_SD_DEMAND

#include "doom_menu.h"
#include "doom_sd.h"

/* Contadores das tabelas dinamicas */
struct doom_dyn_tables
{
    int wad_count;
    int game_count;
    int master_count;
    int master_index; /* indice do item "MASTER LEVELS" no menu, ou -1 */
    int master_iwad;  /* indice da WAD base (doom2.wad) das fases, ou -1 */
};

extern struct doom_dyn_tables doom_dyn;
extern struct doom_wad_entry doom_dyn_wad_table[DOOM_SD_MAX_WADS];
extern struct doom_menu_game doom_dyn_games[DOOM_SD_MAX_WADS + 1];
extern struct doom_menu_master doom_dyn_masters[DOOM_SD_MAX_WADS];

#undef DOOM_WAD_COUNT
#undef DOOM_MENU_GAME_COUNT
#undef DOOM_MENU_MASTER_INDEX
#undef DOOM_MENU_MASTER_IWAD
#undef DOOM_MENU_MASTER_COUNT
#define DOOM_WAD_COUNT (doom_dyn.wad_count)
#define DOOM_MENU_GAME_COUNT (doom_dyn.game_count)
#define DOOM_MENU_MASTER_INDEX (doom_dyn.master_index)
#define DOOM_MENU_MASTER_IWAD (doom_dyn.master_iwad)
#define DOOM_MENU_MASTER_COUNT (doom_dyn.master_count)
#define doom_wad_table doom_dyn_wad_table
#define doom_menu_games doom_dyn_games
#define doom_menu_masters doom_dyn_masters

#endif /* DOOM_SD_DEMAND */

#endif
