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
#include "doom_video.h"

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
}

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
    VSTAT[DOOM_VST_FPS_X100] = fps100;
    *last_ms = ms;
    *last_frame = frame;
}

void doom_video_frame(uint32_t frame)
{
    static uint32_t report_ms;
    static uint32_t report_frame;
    uint32_t ms = DG_GetTicksMs();
    bool new_palette = (palette_changed != 0) || (frame == 1u);

    if (frame == 1u)
    {
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

#ifdef DOOM_VIDEO
    doom_kbd_poll();
#ifndef DOOM_VIDEO_NOVSYNC
    wait_vblank();
#endif
    if (new_palette)
    {
        palette_write();
    }
    frame_write((const uint32_t *)DG_ScreenBuffer);
    if (frame == 1u)
    {
        VID_CONTROL = 1u;
        printf("[DOOM] quadro 1: VIDEO ligado (320x200 indexado, paleta RGB444)\n");
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
