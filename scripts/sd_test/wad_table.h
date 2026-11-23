/* Tabela de teste do driver do cartao no cocotb: uma WAD com endereco da SDRAM (o caminho da JTAG). */
#ifndef DOOM_WAD_TABLE_H
#define DOOM_WAD_TABLE_H
#include "doom_menu.h"
#define DOOM_WAD_COUNT 1
static const struct doom_wad_entry doom_wad_table[DOOM_WAD_COUNT] = { { "doom.wad", 0x40100000U, 12408292U }, };
static const struct doom_menu_game doom_menu_games[] = { { "ULTIMATE DOOM", "4 EPISODES - 36 LEVELS", 0 }, };
#define DOOM_MENU_GAME_COUNT 1
#define DOOM_MENU_MASTER_INDEX -1
#define DOOM_MENU_MASTER_IWAD -1
static const struct doom_menu_master doom_menu_masters[] = { { "", -1, 0 }, };
#define DOOM_MENU_MASTER_COUNT 0
#endif
