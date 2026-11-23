/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Menu de escolha do jogo, no estilo do DOOM (ver doom_menu.h).
 *
 * Tudo que aparece na tela vem da primeira IWAD do menu: o fundo (TITLEPIC, escurecido por um mapa do COLORMAP), a
 * caveira que pisca (M_SKULL1 e M_SKULL2), a fonte pequena (STCFN033 a STCFN095) e a paleta
 * (PLAYPAL). A fonte da WAD e avermelhada: o item escolhido sai como ela e o resto sai em cinza, trocando cada cor
 * da fonte pela cor acinzentada de brilho parecido da paleta (e mais escura nos textos de apoio). A tela e montada num buffer de 320x200 indices e entregue ao VIDEO por doom_video_present.
 *
 * Teclas: setas ou W e S movem, A e D mudam de coluna no Master Levels, Enter, E ou espaco escolhem, Esc ou
 * Backspace voltam.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#include "doom_kbd.h"
#include "doom_menu.h"
#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
#include "doom_sd.h"
#endif
#include "doom_logo.h"
#include "doom_video.h"
#include "wad_table.h"
#include "doom_wad_src.h"

#define SCREEN_W 320
#define SCREEN_H 200

/* Mapas do COLORMAP: 0 e a luz cheia e 31 a mais escura */
#define MAP_BACKGROUND 8
#define MAP_PANEL 14

/* Primeira e ultima letras da fonte (STCFN033 e STCFN095, de ! a _) */
#define FONT_FIRST 33
#define FONT_LAST 95

/* Largura do espaco e espacamento entre linhas da fonte pequena */
#define SPACE_WIDTH 4
#define LINE_HEIGHT 8

/* Metade do periodo do piscar da caveira, em ms */
#define SKULL_BLINK_MS 250u

/* Linhas por coluna da lista do Master Levels */
#define MASTER_ROWS 10

static uint8_t screen[SCREEN_W * SCREEN_H] __attribute__((aligned(4)));
static uint16_t palette12[256];

/* Lumps usados, achados no diretorio da WAD */
static const uint8_t *wad_base;
static const uint8_t *lump_playpal;
static const uint8_t *lump_colormap;
static const uint8_t *lump_title;
static const uint8_t *lump_logo;
static const uint8_t *lump_flat;
static const uint8_t *lump_skull[2];
static const uint8_t *glyph[FONT_LAST + 1];

/* Troca de cores da fonte: cinza para os itens, cinza escuro para os textos de apoio (o item escolhido usa a fonte) */
static uint8_t map_gray[256];
static uint8_t map_dim[256];

/* Le um inteiro de 16 bits sem sinal, em little endian, de qualquer endereco */
static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

/* Le um inteiro de 16 bits com sinal */
static int rd16s(const uint8_t *p)
{
    uint32_t v = rd16(p);

    return (v & 0x8000u) != 0u ? (int)v - 0x10000 : (int)v;
}

/* Le um inteiro de 32 bits, em little endian, de qualquer endereco */
static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

#ifdef DOOM_SD_DEMAND
/*
 * Com as WADs no cartao (modo sob demanda) nao ha imagem na SDRAM: o diretorio da WAD do menu e lido do cartao para
 * menu_dir, e cada lump que o menu usa (paleta, mapa de cores, titulo, caveiras e fonte, uns 90 KB) e lido uma vez para
 * a arena menu_arena; os ponteiros do menu apontam para dentro dela, como apontavam para a imagem.
 */
#define MENU_DIR_MAX 4096u
#define MENU_ARENA_BYTES (160u * 1024u)
static uint32_t menu_wad_sector;
static uint32_t menu_lumps;
static uint32_t menu_arena_used;
static uint8_t menu_dir[MENU_DIR_MAX * 16u] __attribute__((aligned(4)));
static uint8_t menu_arena[MENU_ARENA_BYTES] __attribute__((aligned(4)));

/* Le o cabecalho e o diretorio da WAD que comeca em `sector`; 1 se deu certo */
static int card_wad_open(uint32_t sector)
{
    uint8_t head[12];

    menu_wad_sector = sector;
    menu_arena_used = 0;
    menu_lumps = 0;
    if (doom_wad_read(sector, 0, head, sizeof(head)) != sizeof(head))
    {
        return 0;
    }
    menu_lumps = rd32(head + 4);
    if (menu_lumps == 0u || menu_lumps > MENU_DIR_MAX ||
        doom_wad_read(sector, rd32(head + 8), menu_dir, menu_lumps * 16u) != menu_lumps * 16u)
    {
        menu_lumps = 0;
        return 0;
    }
    return 1;
}

/* Acha o lump no diretorio lido, le seu conteudo para a arena e devolve o ponteiro; NULL se falta ou nao cabe */
static const uint8_t *find_lump_card(const char *name)
{
    for (uint32_t i = 0; i < menu_lumps; i++)
    {
        const uint8_t *entry = menu_dir + 16u * i;

        if (strncmp((const char *)(entry + 8), name, 8) == 0)
        {
            uint32_t size = rd32(entry + 4);
            uint8_t *at = menu_arena + menu_arena_used;

            if (size > MENU_ARENA_BYTES - menu_arena_used ||
                doom_wad_read(menu_wad_sector, rd32(entry), at, size) != size)
            {
                return NULL;
            }
            menu_arena_used = (menu_arena_used + size + 3u) & ~3u;
            return at;
        }
    }
    return NULL;
}
#endif /* DOOM_SD_DEMAND */

/* Acha um lump pelo nome no diretorio da WAD; NULL se nao existe */
static const uint8_t *find_lump(const char *name)
{
#ifdef DOOM_SD_DEMAND
    if (doom_sd_wads_on_card())
    {
        return find_lump_card(name);
    }
#endif
    uint32_t count = rd32(wad_base + 4);
    const uint8_t *dir = wad_base + rd32(wad_base + 8);

    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *entry = dir + 16u * i;

        if (strncmp((const char *)(entry + 8), name, 8) == 0)
        {
            return wad_base + rd32(entry);
        }
    }
    return NULL;
}

/* Brilho de uma cor da PLAYPAL, de 0 a 765 */
static int luma(int index)
{
    return (int)lump_playpal[3 * index] + (int)lump_playpal[3 * index + 1] + (int)lump_playpal[3 * index + 2];
}

/* A cor da paleta e acinzentada: os tres canais quase iguais */
static int is_grayish(int index)
{
    int r = lump_playpal[3 * index];
    int g = lump_playpal[3 * index + 1];
    int b = lump_playpal[3 * index + 2];
    int d1 = r > g ? r - g : g - r;
    int d2 = g > b ? g - b : b - g;

    return d1 < 10 && d2 < 10;
}

/* A cor acinzentada da paleta cujo brilho esta mais perto de `target` */
static uint8_t nearest_gray(int target)
{
    int best = 0;
    int best_diff = 100000;

    for (int j = 0; j < 256; j++)
    {
        if (is_grayish(j))
        {
            int diff = luma(j) - target;

            diff = diff < 0 ? -diff : diff;
            if (diff < best_diff)
            {
                best_diff = diff;
                best = j;
            }
        }
    }
    return (uint8_t)best;
}

/* Prepara as trocas de cor, a paleta do VIDEO e os ponteiros dos lumps; devolve 0 se faltar algum lump */
static int assets_init(const struct doom_wad_entry *wad)
{
    char name[9];

#ifdef DOOM_SD_DEMAND
    if (doom_sd_wads_on_card())
    {
        if (!card_wad_open(wad->addr))
        {
            return 0;
        }
    }
    else
#endif
    {
        wad_base = (const uint8_t *)(uintptr_t)wad->addr;
    }
    lump_playpal = find_lump("PLAYPAL");
    lump_colormap = find_lump("COLORMAP");
    lump_title = find_lump("TITLEPIC");
    lump_logo = find_lump("M_DOOM");
    /* Fundo: um piso do proprio jogo ladrilhado, o primeiro desta lista que a WAD tem (todas tem algum) */
    static const char *const flat_names[] = {"FLOOR7_2", "FLAT5_4", "FLOOR4_8", "FLOOR0_1", "FLAT1", "CEIL3_5", "FLOOR5_1"};

    for (unsigned i = 0; i < sizeof(flat_names) / sizeof(flat_names[0]) && lump_flat == NULL; i++)
    {
        lump_flat = find_lump(flat_names[i]);
    }
    lump_skull[0] = find_lump("M_SKULL1");
    lump_skull[1] = find_lump("M_SKULL2");
    if (lump_playpal == NULL || lump_colormap == NULL)
    {
        return 0;
    }
    for (int c = FONT_FIRST; c <= FONT_LAST; c++)
    {
        snprintf(name, sizeof(name), "STCFN%03d", c);
        glyph[c] = find_lump(name);
    }
    for (int i = 0; i < 256; i++)
    {
        palette12[i] = (uint16_t)(((lump_playpal[3 * i] >> 4) << 8) | ((lump_playpal[3 * i + 1] >> 4) << 4) |
                                  (lump_playpal[3 * i + 2] >> 4));
        /* Os itens saem num cinza claro e os textos de apoio num cinza mais escuro, ambos com o brilho da fonte */
        map_gray[i] = nearest_gray(luma(i) * 3 / 2 + 120);
        map_dim[i] = nearest_gray(luma(i) * 3 / 4 + 60);
    }
    doom_logo_set_palette(palette12);
    return 1;
}

/* Desenha um pixel do buffer, cortado nas bordas */
static void put_pixel(int x, int y, uint8_t value)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
    {
        screen[y * SCREEN_W + x] = value;
    }
}

/*
 * Desenha um patch do DOOM em (x, y), ampliado `scale` vezes, descontando o deslocamento do proprio patch como faz o
 * V_DrawPatch; `map` troca as cores (NULL desenha como esta). O patch e uma lista de colunas, cada uma uma lista de
 * trechos (posicao, comprimento e pixels) terminada por 0xFF.
 */
static void draw_patch(const uint8_t *patch, int x, int y, int scale, const uint8_t *map)
{
    int width = (int)rd16(patch);
    int left = rd16s(patch + 4);
    int top = rd16s(patch + 6);

    x -= left * scale;
    y -= top * scale;
    for (int col = 0; col < width; col++)
    {
        const uint8_t *post = patch + rd32(patch + 8 + 4 * col);

        while (post[0] != 0xFFu)
        {
            int start = post[0];
            int length = post[1];
            const uint8_t *pixels = post + 3;

            for (int i = 0; i < length; i++)
            {
                uint8_t value = map != NULL ? map[pixels[i]] : pixels[i];

                for (int sy = 0; sy < scale; sy++)
                {
                    for (int sx = 0; sx < scale; sx++)
                    {
                        put_pixel(x + col * scale + sx, y + (start + i) * scale + sy, value);
                    }
                }
            }
            post += length + 4;
        }
    }
}

/* Largura em pixels de um texto na fonte pequena, ampliado `scale` vezes */
static int text_width(const char *text, int scale)
{
    int width = 0;

    for (; *text != '\0'; text++)
    {
        int c = (*text >= 'a' && *text <= 'z') ? *text - 'a' + 'A' : *text;

        if (c >= FONT_FIRST && c <= FONT_LAST && glyph[c] != NULL)
        {
            width += (int)rd16(glyph[c]) * scale;
        }
        else
        {
            width += SPACE_WIDTH * scale;
        }
    }
    return width;
}

/* Desenha um texto na fonte pequena em (x, y) */
static void draw_text(const char *text, int x, int y, int scale, const uint8_t *map)
{
    for (; *text != '\0'; text++)
    {
        int c = (*text >= 'a' && *text <= 'z') ? *text - 'a' + 'A' : *text;

        if (c >= FONT_FIRST && c <= FONT_LAST && glyph[c] != NULL)
        {
            draw_patch(glyph[c], x, y, scale, map);
            x += (int)rd16(glyph[c]) * scale;
        }
        else
        {
            x += SPACE_WIDTH * scale;
        }
    }
}

/* Texto centrado na tela */
static void draw_text_centered(const char *text, int y, int scale, const uint8_t *map)
{
    draw_text(text, (SCREEN_W - text_width(text, scale)) / 2, y, scale, map);
}

/* Escurece o retangulo [x0, x1) x [y0, y1) com um mapa do COLORMAP */
static void shade_rect(int x0, int y0, int x1, int y1, int map_index)
{
    const uint8_t *map = lump_colormap + 256 * map_index;

    for (int y = y0; y < y1; y++)
    {
        for (int x = x0; x < x1; x++)
        {
            screen[y * SCREEN_W + x] = map[screen[y * SCREEN_W + x]];
        }
    }
}

/* Fundo: um piso do DOOM (64x64) ladrilhado e escurecido, que deixa o logo e os itens legiveis; preto se a WAD nao traz nenhum */
static void draw_background(void)
{
    memset(screen, 0, sizeof(screen));
    if (lump_flat != NULL && lump_colormap != NULL)
    {
        const uint8_t *map = lump_colormap + 256 * MAP_BACKGROUND;

        for (int y = 0; y < SCREEN_H; y++)
        {
            for (int x = 0; x < SCREEN_W; x++)
            {
                screen[y * SCREEN_W + x] = map[lump_flat[(y & 63) * 64 + (x & 63)]];
            }
        }
    }
}

/* O logo do Insper em vermelho sangue, no canto superior esquerdo (o FPS do jogo fica no direito) */
static void draw_insper_logo(void)
{
    doom_logo_draw(screen, SCREEN_W, 2, 2);
}

/* O logo "DOOM" do menu original, na mesma posicao (94, 2) */
static void draw_logo(void)
{
    if (lump_logo != NULL)
    {
        draw_patch(lump_logo, 94, 2, 1, NULL);
    }
}

/* A caveira do DOOM, que pisca entre dois quadros, com o canto superior esquerdo em (x, y) */
static void draw_skull(int x, int y, int blink)
{
    if (lump_skull[blink] != NULL)
    {
        draw_patch(lump_skull[blink], x, y, 1, NULL);
    }
}

/* Texto de ajuda no rodape */
static void draw_help(const char *text)
{
    draw_text_centered(text, SCREEN_H - 11, 1, map_dim);
}

/* Pagina dos jogos, no desenho do menu principal do DOOM: o logo no alto, os itens na mesma coluna e a caveira que pisca
 * ao lado do escolhido. Em vez de New Game, Options e os outros, cada item e um jogo. */
static void draw_games(int selected, int blink)
{
    const int item_x = 72;
    const int item_y = 64;
    const int line = 16;

    draw_background();
    draw_logo();
    draw_insper_logo();
    for (int i = 0; i < DOOM_MENU_GAME_COUNT; i++)
    {
        draw_text(doom_menu_games[i].title, item_x, item_y + line * i, 2, NULL);
    }
    draw_skull(item_x - 32, item_y + line * selected - 5, blink);
    draw_text_centered(doom_menu_games[selected].subtitle, SCREEN_H - 26, 1, map_gray);
    draw_help("UP DOWN OR W S MOVE   ENTER CHOOSES");
}

/* Pagina do Master Levels: duas colunas de dez fases */
static void draw_masters(int selected, int blink)
{
    char label[16];

    draw_background();
    draw_logo();
    draw_insper_logo();
    shade_rect(28, 44, SCREEN_W - 28, SCREEN_H - 18, MAP_PANEL);
    draw_text_centered("MASTER LEVELS", 46, 2, NULL);
    for (int i = 0; i < DOOM_MENU_MASTER_COUNT; i++)
    {
        int col = i / MASTER_ROWS;
        int row = i % MASTER_ROWS;
        int x = 46 + col * 146;
        int y = 70 + row * 11;
        const uint8_t *map = (i == selected) ? NULL : map_gray;

        draw_text(doom_menu_masters[i].title, x, y, 1, map);
        snprintf(label, sizeof(label), "MAP%02d", doom_menu_masters[i].map);
        draw_text(label, x + 72, y, 1, (i == selected) ? NULL : map_dim);
        if (i == selected)
        {
            draw_skull(x - 34, y - 12, blink);
        }
    }
    draw_help("MOVE   ENTER CHOOSES   ESC BACK");
}

/* Mostra o buffer no VIDEO */
static void present(void)
{
    doom_video_present(screen, palette12);
}

#ifdef DOOM_MENU_SCRIPT
/* Teclas do teste no Spike (DOOM_MENU_SCRIPT): u e d movem, l e r mudam de coluna, e escolhe, b volta, p guarda a tela */
static int scripted_key(int *snapshot)
{
    static const char *script = DOOM_MENU_SCRIPT;

    *snapshot = 0;
    if (*script == 'p')
    {
        *snapshot = 1;
        script++;
        return 0;
    }
    switch (*script == '\0' ? '\0' : *script++)
    {
    case 'u':
        return KEY_UPARROW;
    case 'd':
        return KEY_DOWNARROW;
    case 'l':
        return KEY_LEFTARROW;
    case 'r':
        return KEY_RIGHTARROW;
    case 'e':
        return KEY_ENTER;
    case 'b':
        return KEY_ESCAPE;
    default:
        return -1;
    }
}
#endif

/* Proxima tecla pressionada do teclado (ou do roteiro de teste); 0 se nao ha */
static int next_key(int *snapshot)
{
    *snapshot = 0;
#ifdef DOOM_MENU_SCRIPT
    return scripted_key(snapshot);
#else
    int pressed;
    unsigned char key;

    doom_kbd_poll();
    while (doom_kbd_pop(&pressed, &key))
    {
        if (pressed)
        {
            return key;
        }
    }
    return 0;
#endif
}

/* Traduz a tecla para as acoes do menu */
enum menu_action
{
    ACT_NONE,
    ACT_UP,
    ACT_DOWN,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_SELECT,
    ACT_BACK,
    ACT_END
};

static enum menu_action action_of(int key)
{
    switch (key)
    {
    case KEY_UPARROW:
    case 'w':
        return ACT_UP;
    case KEY_DOWNARROW:
    case 's':
        return ACT_DOWN;
    case KEY_LEFTARROW:
    case 'a':
        return ACT_LEFT;
    case KEY_RIGHTARROW:
    case 'd':
        return ACT_RIGHT;
    case KEY_ENTER:
    case 'e':
    case ' ':
        return ACT_SELECT;
    case KEY_ESCAPE:
    case KEY_BACKSPACE:
        return ACT_BACK;
    case -1:
        return ACT_END;
    default:
        return ACT_NONE;
    }
}


/*
 * Tela de carga: depois da escolha, o jogo leva alguns segundos lendo as texturas e os sprites da fase (do cartao SD,
 * no modo sob demanda) antes do primeiro quadro, e a tela ficaria parada no menu. Esta tela mostra o nome do que esta
 * abrindo, um bloco que anda e quantos MB ja vieram do cartao, e some quando o jogo desenha o primeiro quadro.
 */
static int loading_active;
static char loading_name[48];
static uint32_t loading_base;
static uint32_t loading_last;

/* Repinta o retangulo [x0, x1) x [y0, y1) com o fundo (o piso ladrilhado) */
static void loading_bg_rect(int x0, int y0, int x1, int y1)
{
    const uint8_t *map = lump_colormap + 256 * MAP_BACKGROUND;

    for (int y = y0; y < y1; y++)
    {
        for (int x = x0; x < x1; x++)
        {
            screen[y * SCREEN_W + x] = (lump_flat != NULL) ? map[lump_flat[(y & 63) * 64 + (x & 63)]] : 0u;
        }
    }
}

/* A parte que muda: a barra com o bloco que anda e o contador de MB lidos */
static void draw_loading_dynamic(uint32_t blocks)
{
    char text[40];
    uint32_t tenths = (blocks * 10u) / 2048u;
    uint8_t frame_color = nearest_gray(70);
    uint8_t fill_color = nearest_gray(235);
    int block_x = 62 + (int)((blocks / 24u) % 150u);

    loading_bg_rect(40, 126, 280, 172);
    for (int x = 60; x < 260; x++)
    {
        screen[138 * SCREEN_W + x] = frame_color;
        screen[149 * SCREEN_W + x] = frame_color;
    }
    for (int y = 138; y < 150; y++)
    {
        screen[y * SCREEN_W + 60] = frame_color;
        screen[y * SCREEN_W + 259] = frame_color;
    }
    for (int y = 140; y < 148; y++)
    {
        for (int x = block_x; x < block_x + 46; x++)
        {
            screen[y * SCREEN_W + x] = fill_color;
        }
    }
    snprintf(text, sizeof(text), "READING SD CARD  %u.%u MB", (unsigned)(tenths / 10u), (unsigned)(tenths % 10u));
    draw_text_centered(text, 156, 1, map_gray);
    present();
}

#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
static void loading_tick(uint32_t blocks)
{
    if (loading_active != 0 && blocks - loading_last >= 48u)
    {
        loading_last = blocks;
        draw_loading_dynamic(blocks - loading_base);
    }
}
#endif

/* Desenha a tela de carga de `name` (a parte fixa) e liga o acompanhamento da leitura do cartao */
static void loading_begin(const char *name)
{
    int scale = (text_width(name, 2) <= SCREEN_W - 24) ? 2 : 1;

    snprintf(loading_name, sizeof(loading_name), "%s", name);
    draw_background();
    draw_logo();
    draw_insper_logo();
    draw_text_centered("LOADING", 76, 2, NULL);
    draw_text_centered(loading_name, 100, scale, map_gray);
    loading_active = 1;
#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
    loading_base = doom_sd_blocks_read();
    loading_last = loading_base;
    doom_sd_progress_cb = loading_tick;
#else
    loading_base = 0u;
    loading_last = 0u;
#endif
    draw_loading_dynamic(0u);
#ifdef DOOM_MENU_DUMP
    doom_video_snapshot(screen, palette12);
#endif
}

void doom_menu_loading_end(void)
{
    loading_active = 0;
#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
    doom_sd_progress_cb = NULL;
#endif
}

void doom_menu_run(struct doom_menu_choice *choice)
{
    int page = 0;
    int game = 0;
    int master = 0;
    int last_blink = -1;
    int redraw = 1;
    int have_assets = assets_init(&doom_wad_table[doom_menu_games[0].iwad]);

    /* Sem lumps de arte o menu nao tem como desenhar: escolhe o primeiro jogo */
    if (!have_assets)
    {
        printf("[DOOM] menu: a WAD %s nao tem PLAYPAL e COLORMAP, abrindo o primeiro jogo\n",
               doom_wad_table[doom_menu_games[0].iwad].name);
        choice->iwad = doom_menu_games[0].iwad;
        choice->pwad = -1;
        choice->map = 0;
        return;
    }

    for (;;)
    {
        int blink = (int)((DG_GetTicksMs() / SKULL_BLINK_MS) & 1u);
        int snapshot;
        int key;
        enum menu_action action;

        if (redraw || blink != last_blink)
        {
            if (page == 0)
            {
                draw_games(game, blink);
            }
            else
            {
                draw_masters(master, blink);
            }
            present();
            last_blink = blink;
            redraw = 0;
        }
        key = next_key(&snapshot);
        action = action_of(key);
        if (snapshot)
        {
            doom_video_snapshot(screen, palette12);
        }
        if (action == ACT_NONE)
        {
            continue;
        }
        redraw = 1;
        if (page == 0)
        {
            if (action == ACT_UP && game > 0)
            {
                game--;
            }
            else if (action == ACT_DOWN && game < DOOM_MENU_GAME_COUNT - 1)
            {
                game++;
            }
            else if (action == ACT_SELECT || action == ACT_END)
            {
                if (game == DOOM_MENU_MASTER_INDEX)
                {
                    page = 1;
                }
                else
                {
                    choice->iwad = doom_menu_games[game].iwad;
                    choice->pwad = -1;
                    choice->map = 0;
                    loading_begin(doom_menu_games[game].title);
                    return;
                }
            }
        }
        else
        {
            if (action == ACT_UP && master % MASTER_ROWS > 0)
            {
                master--;
            }
            else if (action == ACT_DOWN && master % MASTER_ROWS < MASTER_ROWS - 1 &&
                     master < DOOM_MENU_MASTER_COUNT - 1)
            {
                master++;
            }
            else if (action == ACT_LEFT && master >= MASTER_ROWS)
            {
                master -= MASTER_ROWS;
            }
            else if (action == ACT_RIGHT && master + MASTER_ROWS < DOOM_MENU_MASTER_COUNT)
            {
                master += MASTER_ROWS;
            }
            else if (action == ACT_BACK)
            {
                page = 0;
            }
            else if (action == ACT_SELECT || action == ACT_END)
            {
                choice->iwad = DOOM_MENU_MASTER_IWAD;
                choice->pwad = doom_menu_masters[master].wad;
                choice->map = doom_menu_masters[master].map;
                {
                    char name[48];

                    snprintf(name, sizeof(name), "MASTER: %s", doom_menu_masters[master].title);
                    loading_begin(name);
                }
                return;
            }
        }
    }
}
