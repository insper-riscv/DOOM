/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Saida de video indexada (VIDEO da plataforma doom) e estatisticas de execucao.
 *
 * Existe so no build com framebuffer indexado (CMAP256, VIDEO=1 no build.sh): o DOOM ja
 * entrega 320x200 bytes de indice (DG_ScreenBuffer) e a paleta (colors[], palette_changed).
 * Com DOOM_VIDEO (placa) o quadro vai para a janela do VIDEO; sem ele (Spike, alvo layout)
 * so ficam as estatisticas e o CRC-32 dos quadros, para comparar com a placa.
 */

#ifndef DOOM_VIDEO_H
#define DOOM_VIDEO_H

#include <stdint.h>

/* Quadros cujo CRC-32 de indices e da paleta vao para o bloco de estatisticas (1 a N) */
#ifndef DOOM_VCRC_FRAMES
#define DOOM_VCRC_FRAMES 32u
#endif

/* Tempo sem nenhum evento de teclado depois do qual o jogo volta ao titulo (modo interativo), em ms: 15 min */
#ifndef DOOM_IDLE_RESET_MS
#define DOOM_IDLE_RESET_MS (15u * 60u * 1000u)
#endif

/* Reinicia o programa por inatividade (port/doom_runtime.c, so DOOM_INTERACTIVE) */
void doom_restart_idle(uint32_t idle_ms);

/* Intervalo entre as linhas de acompanhamento na JTAG UART, em ms */
#define DOOM_REPORT_MS 5000u

/*
 * Bloco de estatisticas na SDRAM (DOOM_VSTAT_ADDR, -D do build, scripts/layout.sh), na
 * ultima pagina que a cache ignora, para o `dump-ram` ler sem a UART. Palavras de 32 bits:
 * cabecalho de 16 e, a partir da palavra 16, o par (CRC-32 dos 64000 indices, CRC-32 da
 * paleta de 256 entradas RGB444) de cada quadro 1 a DOOM_VCRC_FRAMES.
 */
#define DOOM_VSTAT_MAGIC 0x41545356u /* bytes "VSTA" */

enum doom_vstat_word
{
    DOOM_VST_MAGIC = 0,
    DOOM_VST_FRAMES = 1,   /* quadros entregues */
    DOOM_VST_PALETTES = 2, /* vezes que a paleta foi escrita no VIDEO */
    DOOM_VST_PRESSED = 3,  /* eventos de tecla pressionada */
    DOOM_VST_RELEASED = 4, /* eventos de tecla solta */
    DOOM_VST_KBD_LOST = 5, /* estouros e erros do KBD */
    DOOM_VST_MS = 6,       /* DG_GetTicksMs do ultimo quadro */
    DOOM_VST_FPS_X100 = 7, /* fps da ultima janela de 5 s, vezes 100 */
    DOOM_VST_CAPTURED = 8, /* quantos quadros ja tem CRC */
    DOOM_VST_HEAD_WORDS = 16
};

/*
 * Mostra uma tela de 320x200 indices (alinhada a 4 bytes) com a paleta RGB444 dada, e liga a exibicao. E o que o
 * menu de escolha do jogo (doom_menu.c) usa antes de o DOOM desenhar o primeiro quadro.
 */
void doom_video_present(const uint8_t *indices, const uint16_t *palette12);

/*
 * Guarda a tela do menu para conferencia no Spike (DOOM_MENU_DUMP): imprime os indices e a paleta no console, que
 * scripts/menu_png.py converte em PNG. Sem DOOM_MENU_DUMP nao faz nada.
 */
void doom_video_snapshot(const uint8_t *indices, const uint16_t *palette12);

/* Chamada por DG_DrawFrame com o numero do quadro (1, 2, ...): apresenta e atualiza as estatisticas */
void doom_video_frame(uint32_t frame);

#endif
