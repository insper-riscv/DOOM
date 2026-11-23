/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Saida de video indexada do DOOM na plataforma doom e estatisticas da execucao.
 *
 * Com CMAP256 a doomgeneric ja desenha 320x200 bytes de indice de paleta em DG_ScreenBuffer
 * (o formato nativo do DOOM, sem a conversao para XRGB de 32 bits que custava 60% das
 * instrucoes de um quadro). A janela do VIDEO tem o mesmo formato, entao o quadro vai com
 * stores de palavra, quatro pixels cada, sem leitura antes da escrita. A paleta (256
 * entradas de 12 bits, RGB444, vermelho nos bits 11:8) so e reescrita quando o DOOM a muda.
 *
 * Registradores (Peripherals docs/pt-br/video/video.md, secao 4; enderecos em bytes a
 * partir de RV32_VIDEO_BASE): controle +0 (bit 0 liga a exibicao), contador de quadros +8,
 * paleta +0x400 (uma palavra por entrada) e framebuffer +0x20000 (somente escrita).
 *
 * Para nao rasgar a imagem, o quadro so e escrito logo depois do inicio do apagamento
 * vertical (o contador de quadros mudou): a escrita, de uns 2 ms, anda mais rapida que a
 * varredura de 16,8 ms e fica sempre a frente dela. DOOM_VIDEO_NOVSYNC tira a espera.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "doomgeneric.h"
#include "i_video.h"
#include "doomstat.h"

#include "doom_kbd.h"
#include "doom_logo.h"
#ifdef DOOM_MENU
#include "doom_menu.h"
#endif
#include "doom_platform.h"
#include "doom_video.h"

#ifndef DEATH_MENU_MS
#define DEATH_MENU_MS 5000u /* a animacao de morte termina antes; depois o menu abre sozinho */
#endif

#ifdef DOOM_VIDEO
#include "memory_map.h"

#define VID_CONTROL (*(volatile uint32_t *)(RV32_VIDEO_BASE + 0u))
#define VID_FRAMES (*(volatile uint32_t *)(RV32_VIDEO_BASE + 8u))
#define VID_PALETTE ((volatile uint32_t *)(RV32_VIDEO_BASE + 0x400u))
#define VID_FB ((volatile uint32_t *)(RV32_VIDEO_BASE + 0x20000u))

/* Espera maxima pelo inicio do apagamento vertical (um quadro dura 16,8 ms), em microssegundos do CSR time */
#define VSYNC_TIMEOUT_US 40000u
#endif

/* Bloco de estatisticas na SDRAM (doom_video.h) */
#define VSTAT ((volatile uint32_t *)(uintptr_t)DOOM_VSTAT_ADDR)

/* Pixels do framebuffer e palavras que ocupam */
#define FB_PIXELS ((uint32_t)DOOMGENERIC_RESX * (uint32_t)DOOMGENERIC_RESY)
#define FB_WORDS (FB_PIXELS / 4u)

/* Paleta aplicada, RGB444 por entrada */
static uint16_t palette12[256];
static uint32_t palette_writes;

/* CRC-32 (IEEE) por tabela, construida na primeira chamada */
static uint32_t crc_table[256];
static bool crc_ready;

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        uint32_t c = i;

        for (int b = 0; b < 8; b++)
        {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        crc_table[i] = c;
    }
    crc_ready = true;
}

static uint32_t crc32_bytes(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu;

    if (!crc_ready)
    {
        crc_init();
    }
    for (uint32_t i = 0; i < n; i++)
    {
        crc = crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

#ifdef DOOM_FPS_HUD
extern int viewwidth;
extern int viewwindowy;
static void hud_pick_colors(void);
#endif

/* Converte colors[] (8 bits por canal) para RGB444, tomando os 4 bits altos de cada canal */
static void palette_convert(void)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        uint32_t r = colors[i].r >> 4;
        uint32_t g = colors[i].g >> 4;
        uint32_t b = colors[i].b >> 4;

        palette12[i] = (uint16_t)((r << 8) | (g << 4) | b);
    }
#ifdef DOOM_FPS_HUD
    hud_pick_colors();
    doom_logo_set_palette(palette12);
#endif
}


#ifdef DOOM_FPS_HUD
/*
 * Indicador de FPS em amarelo no canto superior direito, como o do Fraps (HUD=1 no build.sh; a imagem da placa o liga
 * por padrao). Mostra os quadros por segundo do ultimo segundo. O desenho entra depois do CRC dos quadros, que nao o
 * enxerga. No jogo normal o numero nunca passa dos 35 tics por segundo do DOOM.
 */
/* Glifos 3x5, uma linha de 3 bits por tres bits da palavra (linha de cima nos bits altos) */
static uint16_t hud_glyph(char c)
{
    switch (c)
    {
    case '0': return 0x7B6F;
    case '1': return 0x2C97;
    case '2': return 0x73E7;
    case '3': return 0x73CF;
    case '4': return 0x5BC9;
    case '5': return 0x79CF;
    case '6': return 0x79EF;
    case '7': return 0x7252;
    case '8': return 0x7BEF;
    case '9': return 0x7BCF;
    case 'F': return 0x79E4;
    case 'P': return 0x7BE4;
    case 'S': return 0x79CF;
    case 'M': return 0x5FED;
    case 'A': return 0x2BED;
    case 'X': return 0x5AAD;
    default: return 0u;
    }
}

static uint8_t hud_fg;
static uint8_t hud_bg;
static uint32_t hud_fps;

/* Escolhe o amarelo mais vivo e o preto mais escuro da paleta atual */
static void hud_pick_colors(void)
{
    static int fixed;

    /* So a primeira paleta escolhe as cores, como o resto do jogo: os tingimentos de dano e item mudam as cores da cena
     * pelos mesmos indices */
    if (fixed != 0)
    {
        return;
    }
    fixed = 1;
    int32_t best_y = -1000;
    uint32_t best_lo = 0xFFFFu;

    for (uint32_t i = 0; i < 256u; i++)
    {
        int32_t r = (int32_t)((uint32_t)palette12[i] >> 8);
        int32_t g = (int32_t)((palette12[i] >> 4) & 0xFu);
        int32_t b = (int32_t)(palette12[i] & 0xFu);
        int32_t m = (r < g) ? r : g;
        int32_t y = 2 * m - b + (r + g) / 4;
        uint32_t v = (uint32_t)(r + g + b);

        if (y > best_y)
        {
            best_y = y;
            hud_fg = (uint8_t)i;
        }
        if (v < best_lo)
        {
            best_lo = v;
            hud_bg = (uint8_t)i;
        }
    }
}

/* Desenha so os pixels acesos de um caractere com escala 2 em (x, y), na cor dada */
static void hud_char(uint8_t *fb, uint32_t x, uint32_t y, char c, uint8_t color)
{
    uint16_t g = hud_glyph(c);

    for (uint32_t row = 0; row < 5u; row++)
    {
        for (uint32_t col = 0; col < 3u; col++)
        {
            if (((g >> (14u - 3u * row - col)) & 1u) != 0u)
            {
                uint8_t *p = fb + (y + 2u * row) * DOOMGENERIC_RESX + x + 2u * col;

                p[0] = color;
                p[1] = color;
                p[DOOMGENERIC_RESX] = color;
                p[DOOMGENERIC_RESX + 1u] = color;
            }
        }
    }
}

/* Uma linha "RRR nnn" de seis caracteres a partir de (x, y): sombra preta e depois os numeros amarelos, sem caixa */
static void hud_line(uint8_t *fb, uint32_t x, uint32_t y, const char *label, uint32_t value)
{
    char text[6];

    text[0] = label[0];
    text[1] = label[1];
    text[2] = label[2];
    text[3] = (value >= 100u) ? (char)('0' + (value / 100u) % 10u) : ' ';
    text[4] = (value >= 10u) ? (char)('0' + (value / 10u) % 10u) : ' ';
    text[5] = (char)('0' + value % 10u);
    for (uint32_t i = 0; i < 6u; i++)
    {
        hud_char(fb, x + 8u * i + 1u, y + 1u, text[i], hud_bg);
    }
    for (uint32_t i = 0; i < 6u; i++)
    {
        hud_char(fb, x + 8u * i, y, text[i], hud_fg);
    }
}

/* Atualiza o valor a cada segundo e desenha a linha */
static void hud_draw(uint8_t *fb, uint32_t ms)
{
    static uint32_t t0_ms;
    static uint32_t frames;

    frames++;
    if ((uint32_t)(ms - t0_ms) >= 1000u)
    {
        hud_fps = (frames * 1000u + (ms - t0_ms) / 2u) / (ms - t0_ms);
        t0_ms = ms;
        frames = 0u;
    }
    /*
     * O HUD escreve direto no buffer do jogo (sem a copia do quadro). Numa partida com a vista inteira o jogo redesenha
     * esse canto a cada quadro, entao nao fica resto; nas telas estaticas (titulo, intervalo, vista reduzida) uma caixa
     * escura apaga o numero anterior.
     */
    if (gamestate != GS_LEVEL || viewwidth != DOOMGENERIC_RESX || viewwindowy != 0)
    {
        for (uint32_t y = 1u; y < 15u; y++)
        {
            for (uint32_t x = DOOMGENERIC_RESX - 54u; x < DOOMGENERIC_RESX - 1u; x++)
            {
                fb[y * DOOMGENERIC_RESX + x] = hud_bg;
            }
        }
    }
    hud_line(fb, DOOMGENERIC_RESX - 52u, 3u, "FPS", hud_fps);
}
#endif /* DOOM_FPS_HUD */

#ifdef DOOM_VIDEO

#ifndef DOOM_VIDEO_NOVSYNC

/* CSR time (1 MHz), so os 32 bits baixos bastam para intervalos curtos */
static uint32_t time_us(void)
{
    uint32_t t;

    __asm__ volatile("csrr %0, time" : "=r"(t));
    return t;
}

/* Espera o contador de quadros do VIDEO mudar (inicio do apagamento), lendo o teclado enquanto isso */
static void wait_vblank(void)
{
    uint32_t frames = VID_FRAMES;
    uint32_t start = time_us();

    while (VID_FRAMES == frames && (uint32_t)(time_us() - start) < VSYNC_TIMEOUT_US)
    {
        doom_kbd_poll();
    }
}

#endif /* DOOM_VIDEO_NOVSYNC */

static void palette_write(void)
{
    for (uint32_t i = 0; i < 256u; i++)
    {
        VID_PALETTE[i] = palette12[i];
    }
}

/* Copia o quadro para a janela do VIDEO: stores de palavra, quatro pixels cada, oito por volta */
static void frame_write(const uint32_t *src)
{
    volatile uint32_t *dst = VID_FB;

    for (uint32_t i = 0; i < FB_WORDS; i += 8u)
    {
        uint32_t a0 = src[i + 0u];
        uint32_t a1 = src[i + 1u];
        uint32_t a2 = src[i + 2u];
        uint32_t a3 = src[i + 3u];
        uint32_t a4 = src[i + 4u];
        uint32_t a5 = src[i + 5u];
        uint32_t a6 = src[i + 6u];
        uint32_t a7 = src[i + 7u];

        dst[i + 0u] = a0;
        dst[i + 1u] = a1;
        dst[i + 2u] = a2;
        dst[i + 3u] = a3;
        dst[i + 4u] = a4;
        dst[i + 5u] = a5;
        dst[i + 6u] = a6;
        dst[i + 7u] = a7;
    }
}

#endif /* DOOM_VIDEO */

#ifdef DOOM_PROFILE
/*
 * Perfil por regiao (PROFILE=1 no build.sh), em ciclos do CSR cycle, somados por janela de DOOM_REPORT_MS e impressos
 * como media por quadro. periodo: entre duas entradas de doom_video_frame (o quadro todo); copia: o memcpy do
 * I_FinishUpdate (da entrada do I_FinishUpdate, envolvido por --wrap, ate a entrada de doom_video_frame); prep:
 * paleta, CRC e teclado antes da escrita; vsync: espera do apagamento vertical; escrita: paleta e 64000 bytes ao
 * VIDEO; o resto do periodo (jogo e renderizacao) e periodo menos as quatro.
 */
uint64_t doom_prof_fb_t0;
static uint64_t pf_last_entry;
static uint64_t pf_period;
static uint64_t pf_copy;
static uint64_t pf_prep;
static uint64_t pf_vsync;
static uint64_t pf_write;
static uint64_t pf_instret;
static uint64_t pf_last_instret;
static uint32_t pf_frames;

extern void __real_I_FinishUpdate(void);

void __wrap_I_FinishUpdate(void)
{
    doom_prof_fb_t0 = doom_read_cycle();
    __real_I_FinishUpdate();
}
#endif

/* Linha de acompanhamento da JTAG UART, a cada DOOM_REPORT_MS */
static void report(uint32_t frame, uint32_t ms, uint32_t *last_ms, uint32_t *last_frame)
{
    const struct doom_kbd_stats *k = doom_kbd_get_stats();
    char events[128];
    uint32_t lost = doom_kbd_take_log(events, sizeof(events));
    uint32_t dt = ms - *last_ms;
    uint32_t df = frame - *last_frame;
    uint32_t fps100 = (dt != 0u) ? (uint32_t)(((uint64_t)df * 100000u) / dt) : 0u;

    /* Estado do jogo: gs 3 e a tela de titulo (GS_DEMOSCREEN), gs 0 com demo=1 e o demo de atracao, gs 0 com demo=0 e o jogo */
    printf("[DOOM t=%lus] fps=%lu.%02lu quadros=%lu gs=%d demo=%d gametic=%d paletas=%lu teclas: +%lu -%lu perdas=%lu eventos=[%s]%s\n",
           (unsigned long)(ms / 1000u), (unsigned long)(fps100 / 100u), (unsigned long)(fps100 % 100u),
           (unsigned long)frame, (int)gamestate, (int)demoplayback, (int)gametic,
           (unsigned long)palette_writes, (unsigned long)k->pressed,
           (unsigned long)k->released, (unsigned long)(k->overflows + k->errors + k->dropped), events,
           lost != 0u ? " (+mais)" : "");
    uint32_t captured = VSTAT[DOOM_VST_CAPTURED];

    if (captured != 0u)
    {
        static const uint32_t shown[] = { 1u, 2u, 4u, 8u, 16u, 32u };

        printf("[DOOM] CRC dos indices:");
        for (unsigned i = 0; i < sizeof(shown) / sizeof(shown[0]); i++)
        {
            if (shown[i] <= captured && shown[i] <= DOOM_VCRC_FRAMES)
            {
                printf(" q%lu=%08lx", (unsigned long)shown[i],
                       (unsigned long)VSTAT[DOOM_VST_HEAD_WORDS + 2u * (shown[i] - 1u)]);
            }
        }
        printf(" pal1=%08lx\n", (unsigned long)VSTAT[DOOM_VST_HEAD_WORDS + 1u]);
    }
#ifdef DOOM_PROFILE
    if (pf_frames != 0u)
    {
        uint64_t n = pf_frames;
        uint64_t rest = pf_period - pf_copy - pf_prep - pf_vsync - pf_write;

        printf("[PERFIL] kciclos por quadro: periodo=%lu copia=%lu prep=%lu vsync=%lu escrita=%lu resto=%lu; kinstr por quadro=%lu\n",
               (unsigned long)(pf_period / n / 1000u), (unsigned long)(pf_copy / n / 1000u),
               (unsigned long)(pf_prep / n / 1000u), (unsigned long)(pf_vsync / n / 1000u),
               (unsigned long)(pf_write / n / 1000u), (unsigned long)(rest / n / 1000u),
               (unsigned long)(pf_instret / n / 1000u));
    }
    pf_period = pf_copy = pf_prep = pf_vsync = pf_write = pf_instret = 0u;
    pf_frames = 0u;
#endif
    VSTAT[DOOM_VST_FPS_X100] = fps100;
    *last_ms = ms;
    *last_frame = frame;
}

/* Mostra uma tela do menu: paleta e quadro direto no VIDEO, e liga a exibicao */
void doom_video_present(const uint8_t *indices, const uint16_t *palette12_in)
{
#ifdef DOOM_VIDEO
    for (uint32_t i = 0; i < 256u; i++)
    {
        VID_PALETTE[i] = palette12_in[i];
    }
    frame_write((const uint32_t *)(const void *)indices);
    VID_CONTROL = 1u;
#else
    (void)indices;
    (void)palette12_in;
#endif
}

/* Imprime a tela do menu no console, para o Spike (DOOM_MENU_DUMP) */
void doom_video_snapshot(const uint8_t *indices, const uint16_t *palette12_in)
{
#ifdef DOOM_MENU_DUMP
    static const char hex[] = "0123456789abcdef";
    char line[2u * (uint32_t)DOOMGENERIC_RESX + 1u];

    printf("MENUDUMP BEGIN\n");
    for (uint32_t y = 0; y < (uint32_t)DOOMGENERIC_RESY; y++)
    {
        for (uint32_t x = 0; x < (uint32_t)DOOMGENERIC_RESX; x++)
        {
            uint8_t v = indices[y * (uint32_t)DOOMGENERIC_RESX + x];

            line[2u * x] = hex[v >> 4];
            line[2u * x + 1u] = hex[v & 0xFu];
        }
        line[2u * (uint32_t)DOOMGENERIC_RESX] = '\0';
        printf("%s\n", line);
    }
    for (uint32_t i = 0; i < 256u; i++)
    {
        printf("%03x\n", (unsigned)palette12_in[i]);
    }
    printf("MENUDUMP END\n");
#else
    (void)indices;
    (void)palette12_in;
#endif
}

void doom_video_frame(uint32_t frame)
{
    static uint32_t report_ms;
    static uint32_t report_frame;
#ifdef DOOM_PROFILE
    uint64_t pc0 = doom_read_cycle();
    uint64_t pc1 = 0u;
    uint64_t pc2 = 0u;
    uint64_t pc3 = 0u;
#endif
    uint32_t ms = DG_GetTicksMs();
    bool new_palette = (palette_changed != 0) || (frame == 1u);

    if (frame == 1u)
    {
#ifdef DOOM_MENU
        /* O primeiro quadro do jogo substitui a tela de carga do menu */
        doom_menu_loading_end();
#endif
        /* Zera o que sobrou de uma execucao anterior na SDRAM */
        VSTAT[DOOM_VST_MAGIC] = 0u;
        VSTAT[DOOM_VST_CAPTURED] = 0u;
    }
    if (new_palette)
    {
        palette_convert();
        palette_changed = false;
        palette_writes++;
    }

    /* CRC-32 dos indices e da paleta dos primeiros quadros, para comparar com o Spike */
    if (frame <= DOOM_VCRC_FRAMES)
    {
        uint32_t slot = DOOM_VST_HEAD_WORDS + 2u * (frame - 1u);

        VSTAT[slot] = crc32_bytes((const uint8_t *)DG_ScreenBuffer, FB_PIXELS);
        VSTAT[slot + 1u] = crc32_bytes((const uint8_t *)palette12, sizeof(palette12));
        VSTAT[DOOM_VST_CAPTURED] = frame;
    }

#ifdef DOOM_FPS_HUD
    hud_draw((uint8_t *)DG_ScreenBuffer, ms);
#endif
#ifdef DOOM_PROFILE
    pc1 = doom_read_cycle();
#endif
#ifdef DOOM_VIDEO
    doom_kbd_poll();
#ifndef DOOM_VIDEO_NOVSYNC
    wait_vblank();
#endif
#ifdef DOOM_PROFILE
    pc2 = doom_read_cycle();
#endif
    if (new_palette)
    {
        palette_write();
    }
    frame_write((const uint32_t *)DG_ScreenBuffer);
#ifdef DOOM_PROFILE
    pc3 = doom_read_cycle();
#endif
    if (frame == 1u)
    {
        VID_CONTROL = 1u;
        printf("[DOOM] quadro 1: VIDEO ligado (320x200 indexado, paleta RGB444)\n");
    }
#endif

#if defined(DOOM_INTERACTIVE) && !defined(DOOM_MEASURE)
    /*
     * Jogador morto: o DOOM original deixa a tela parada na vista do corpo e so reinicia a fase com o botao de usar,
     * sem nenhum aviso. Aqui a mensagem "VOCE MORREU" ensina o botao e, passados DEATH_MENU_MS (5 s), o menu do
     * jogo abre sozinho (Novo Jogo, Carregar, Sair). Se o jogador reiniciar antes, o contador zera.
     */
    {
        static uint32_t dead_since_ms;
        static uint32_t dead_msg_ms;
        static bool was_dead;
        static bool menu_done;
        extern void M_StartControlPanel(void);

        if (gamestate == GS_LEVEL && !demoplayback && players[consoleplayer].playerstate == PST_DEAD)
        {
            if (!was_dead)
            {
                was_dead = true;
                menu_done = false;
                dead_since_ms = ms;
                dead_msg_ms = ms;
            }
            /* Mensagem depois da animacao de queda (1,5 s) e a cada 3 s */
            if (!menuactive && (uint32_t)(ms - dead_since_ms) >= 1500u && (uint32_t)(ms - dead_msg_ms) >= 3000u)
            {
                dead_msg_ms = ms;
                players[consoleplayer].message = (char *)"YOU DIED! PRESS ENTER OR E TO RESTART";
            }
            if (!menuactive && !menu_done && (uint32_t)(ms - dead_since_ms) >= DEATH_MENU_MS)
            {
                M_StartControlPanel();
                menu_done = true; /* nao reabre o menu depois de fechado */
            }
        }
        else
        {
            was_dead = false;
        }
    }
#endif
#ifdef DOOM_INTERACTIVE
    /*
     * Inatividade: qualquer byte do teclado (pressionar, soltar, repeticao) recomeca a contagem, feita pelo relogio
     * do jogo (DG_GetTicksMs). Passado DOOM_IDLE_RESET_MS sem eventos, o jogo reinicia se estiver numa partida, no
     * menu aberto, na tela de intervalo ou no final; no titulo e no demo de atracao (menu fechado) nao faz nada. O
     * contador comeca no quadro 1 de cada boot (o relogio e o bss recomecam no restart), entao nunca dispara no boot.
     */
    static uint32_t idle_since;
    static uint32_t idle_bytes;
    uint32_t bytes = doom_kbd_get_stats()->bytes;

    if (frame == 1u || bytes != idle_bytes)
    {
        idle_since = ms;
        idle_bytes = bytes;
    }
    else if ((uint32_t)(ms - idle_since) >= DOOM_IDLE_RESET_MS &&
             (menuactive || (gamestate != GS_DEMOSCREEN && !demoplayback)))
    {
        doom_restart_idle(ms - idle_since);
    }
#endif

#ifdef DOOM_PROFILE
    if (frame > 1u)
    {
        uint64_t ins = doom_read_instret();

        pf_period += pc0 - pf_last_entry;
        pf_copy += pc0 - doom_prof_fb_t0;
        pf_prep += pc1 - pc0;
        pf_vsync += pc2 - pc1;
        pf_write += pc3 - pc2;
        pf_instret += ins - pf_last_instret;
        pf_frames++;
    }
    pf_last_entry = pc0;
    pf_last_instret = doom_read_instret();
#endif

    if (frame == 1u)
    {
        report_ms = ms;
        report_frame = 1u;
    }
    else if ((uint32_t)(ms - report_ms) >= DOOM_REPORT_MS)
    {
        report(frame, ms, &report_ms, &report_frame);
    }

    const struct doom_kbd_stats *k = doom_kbd_get_stats();

    VSTAT[DOOM_VST_FRAMES] = frame;
    VSTAT[DOOM_VST_PALETTES] = palette_writes;
    VSTAT[DOOM_VST_PRESSED] = k->pressed;
    VSTAT[DOOM_VST_RELEASED] = k->released;
    VSTAT[DOOM_VST_KBD_LOST] = k->overflows + k->errors + k->dropped;
    VSTAT[DOOM_VST_MS] = ms;
    __sync_synchronize();
    VSTAT[DOOM_VST_MAGIC] = DOOM_VSTAT_MAGIC;
}
