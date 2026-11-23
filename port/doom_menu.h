/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Menu de escolha do jogo, mostrado antes do DOOM (MENU=1 no build.sh).
 *
 * O menu desenha com os graficos da propria WAD (logo, caveira, fonte e fundo) numa tela de 320x200 indexada, e
 * deixa escolher entre os jogos que o scripts/wads.sh colocou na SDRAM e as fases do Master Levels. A tabela de WADs
 * (wad_table.h) e gerada por esse script a partir das WADs da pasta que o usuario indicou.
 */

#ifndef DOOM_MENU_H
#define DOOM_MENU_H

#include <stdint.h>

/* Uma WAD na SDRAM: nome em minusculas, endereco e tamanho em bytes */
struct doom_wad_entry
{
    const char *name;
    uint32_t addr;
    uint32_t size;
};

/* Um jogo do menu: titulo, descricao e indice da IWAD na tabela (-1 para o Master Levels) */
struct doom_menu_game
{
    const char *title;
    const char *subtitle;
    int iwad;
};

/* Uma fase do Master Levels: titulo, indice da PWAD na tabela e mapa em que entra no DOOM II */
struct doom_menu_master
{
    const char *title;
    int wad;
    int map;
};

/* A escolha: indice da IWAD, indice da PWAD (-1 sem PWAD) e mapa a abrir (0 para a tela de titulo) */
struct doom_menu_choice
{
    int iwad;
    int pwad;
    int map;
};

/* Mostra o menu, espera a escolha e a devolve em *choice */
void doom_menu_run(struct doom_menu_choice *choice);

#endif
