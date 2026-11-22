/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Teclado PS/2 da plataforma doom: scancodes do conjunto 2 para as teclas do DOOM.
 *
 * O teclado manda um byte de make ao pressionar, repete o make enquanto a tecla segue
 * pressionada (repeticao automatica), e manda F0 e o mesmo byte ao soltar; as teclas
 * estendidas vem com o prefixo E0 e Pause com a sequencia E1. Os bytes FA (resposta), AA
 * (autoteste), FE (reenvio), 00 (excesso) e FF (erro) nao sao teclas e sao descartados.
 *
 * Mapa de jogo (doomkeys.h):
 *   W A S D                          frente, tras e deslizar de lado (ver __wrap_I_InitInput)
 *   E                                usar / abrir porta   Espaco (29)        atirar
 *   setas esquerda e direita         girar (as quatro setas navegam nos menus)
 *   Ctrl (14 e E0 14)                atirar tambem        Shift (12 e 59)    correr
 *   Alt (11 e E0 11)                 com as setas, deslizar de lado
 *   Enter (5A e E0 5A), Esc (76), Tab (0D), Backspace (66), F1 a F12
 *   letras A a Z, 0 a 9, '-', '=', ',' e '.' como o ASCII que os menus e os cheats esperam
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doomkeys.h"
#include "doom_kbd.h"
#ifndef DOOM_KBD_HOST_TEST
#include "doomstat.h"
#endif

#ifdef DOOM_VIDEO
#include "memory_map.h"

/* Registradores do KBD (ps2_keyboard.md, secao 3), em bytes a partir de RV32_KBD_BASE */
#define KBD_DATA (*(volatile uint32_t *)(RV32_KBD_BASE + 0u))
#define KBD_STATUS (*(volatile uint32_t *)(RV32_KBD_BASE + 4u))
#define KBD_RX_VALID 0x01u
#define KBD_RX_OVERFLOW 0x04u
#define KBD_RX_ERROR 0x08u

/* Maximo de bytes lidos por chamada de doom_kbd_poll (a fila de hardware tem 16) */
#define KBD_POLL_MAX 64u
#endif

/* Codigos do conjunto 2 vao de 0x00 a 0x83 (F7) */
#define SET2_SIZE 0x84u

/* Sem prefixo: so o que o jogo usa; zero e ignorado */
static const uint8_t normal_map[SET2_SIZE] = {
    /* letras */
    [0x1C] = 'a', [0x32] = 'b', [0x21] = 'c', [0x23] = 'd', [0x24] = 'e', [0x2B] = 'f',
    [0x34] = 'g', [0x33] = 'h', [0x43] = 'i', [0x3B] = 'j', [0x42] = 'k', [0x4B] = 'l',
    [0x3A] = 'm', [0x31] = 'n', [0x44] = 'o', [0x4D] = 'p', [0x15] = 'q', [0x2D] = 'r',
    [0x1B] = 's', [0x2C] = 't', [0x3C] = 'u', [0x2A] = 'v', [0x1D] = 'w', [0x22] = 'x',
    [0x35] = 'y', [0x1A] = 'z',
    /* digitos da fileira superior, '-' e '=' */
    [0x45] = '0', [0x16] = '1', [0x1E] = '2', [0x26] = '3', [0x25] = '4', [0x2E] = '5',
    [0x36] = '6', [0x3D] = '7', [0x3E] = '8', [0x46] = '9', [0x4E] = KEY_MINUS, [0x55] = KEY_EQUALS,
    /* virgula e ponto deslizam de lado */
    [0x41] = ',', [0x49] = '.',
    /* acoes */
    [0x14] = ' ', [0x29] = ' ', [0x12] = KEY_RSHIFT, [0x59] = KEY_RSHIFT, [0x11] = KEY_LALT,
    [0x5A] = KEY_ENTER, [0x76] = KEY_ESCAPE, [0x0D] = KEY_TAB, [0x66] = KEY_BACKSPACE,
    /* F1 a F12 */
    [0x05] = KEY_F1, [0x06] = KEY_F2, [0x04] = KEY_F3, [0x0C] = KEY_F4, [0x03] = KEY_F5,
    [0x0B] = KEY_F6, [0x83] = KEY_F7, [0x0A] = KEY_F8, [0x01] = KEY_F9, [0x09] = KEY_F10,
    [0x78] = KEY_F11, [0x07] = KEY_F12,
};

/* Com o prefixo E0; E0 12 e E0 F0 12 (Shift falso do PrtSc e do Insert) ficam de fora de proposito */
static const uint8_t ext_map[SET2_SIZE] = {
    [0x75] = KEY_UPARROW, [0x72] = KEY_DOWNARROW, [0x6B] = KEY_LEFTARROW, [0x74] = KEY_RIGHTARROW,
    [0x14] = ' ', [0x11] = KEY_LALT, [0x5A] = KEY_ENTER,
};

/* Fila de eventos de software: pressed no byte alto e a tecla no baixo */
#define QUEUE_SIZE 64u
static uint16_t queue[QUEUE_SIZE];
static uint32_t queue_head;
static uint32_t queue_tail;

/*
 * Teclas fisicas (codigo e prefixo) hoje pressionadas: guarda a tecla do DOOM que o make entregou (zero se solta), e
 * a soltura entrega a mesma, mesmo que o menu tenha aberto ou fechado no meio (nenhuma tecla fica presa)
 */
static uint8_t emitted[2][SET2_SIZE];

/* Diz se o menu esta ativo e nao em digitacao de texto (WASD e E navegam); NULL ou 0: letras normais */
static int (*menu_nav_hook)(void);

void doom_kbd_set_menu_hook(int (*hook)(void))
{
    menu_nav_hook = hook;
}

/* No menu, W S A D e E viram as setas e o Enter */
static uint8_t menu_translate(uint8_t key)
{
    switch (key)
    {
    case 'w':
        return KEY_UPARROW;
    case 's':
        return KEY_DOWNARROW;
    case 'a':
        return KEY_LEFTARROW;
    case 'd':
        return KEY_RIGHTARROW;
    case 'e':
        return KEY_ENTER;
    default:
        return key;
    }
}

/* Estado do decodificador */
static bool seen_ext;
static bool seen_break;
static uint32_t pause_left;

static struct doom_kbd_stats stats;

/* Registro textual dos eventos desde a ultima descarga */
#define LOG_SIZE 160u
static char log_buf[LOG_SIZE];
static uint32_t log_len;
static uint32_t log_lost;

static void log_event(bool ext, uint8_t code, bool pressed)
{
    static const char hex[] = "0123456789ABCDEF";

    /* "+75e " tem no maximo 5 caracteres */
    if (log_len + 5u >= LOG_SIZE)
    {
        log_lost++;
        return;
    }
    log_buf[log_len++] = pressed ? '+' : '-';
    log_buf[log_len++] = hex[code >> 4];
    log_buf[log_len++] = hex[code & 15u];
    if (ext)
    {
        log_buf[log_len++] = 'e';
    }
    log_buf[log_len++] = ' ';
}

static void queue_push(bool pressed, uint8_t key)
{
    uint32_t next = (queue_head + 1u) % QUEUE_SIZE;

    if (next == queue_tail)
    {
        stats.dropped++;
        return;
    }
    queue[queue_head] = (uint16_t)(((pressed ? 1u : 0u) << 8) | key);
    queue_head = next;
}

/* Trata uma tecla completa (codigo com ou sem prefixo, make ou break) */
static void key_event(bool ext, uint8_t code, bool pressed)
{
    if (code >= SET2_SIZE)
    {
        return;
    }
    uint8_t *slot = &emitted[ext ? 1u : 0u][code];
    uint8_t key;

    if (pressed)
    {
        key = ext ? ext_map[code] : normal_map[code];
        if (key == 0u || *slot != 0u)
        {
            /* Tecla sem funcao, ou repeticao de uma ja pressionada */
            return;
        }
        if (menu_nav_hook != NULL && menu_nav_hook() != 0)
        {
            key = menu_translate(key);
        }
        *slot = key;
    }
    else
    {
        key = *slot;
        if (key == 0u)
        {
            /* Soltura de uma tecla que nao estava pressionada */
            return;
        }
        *slot = 0u;
    }
    queue_push(pressed, key);
    log_event(ext, code, pressed);
    if (pressed)
    {
        stats.pressed++;
    }
    else
    {
        stats.released++;
    }
}

void doom_kbd_feed(uint8_t byte)
{
    stats.bytes++;
    if (pause_left > 0u)
    {
        /* Resto da sequencia E1 14 77 E1 F0 14 F0 77 do Pause: sem tecla de jogo */
        pause_left--;
        return;
    }
    switch (byte)
    {
    case 0xE0:
        seen_ext = true;
        return;
    case 0xE1:
        pause_left = 7u;
        return;
    case 0xF0:
        seen_break = true;
        return;
    case 0xFA:
    case 0xAA:
    case 0xFE:
    case 0x00:
    case 0xFF:
        if (!seen_ext && !seen_break)
        {
            /* Resposta, autoteste, reenvio, excesso ou erro do teclado */
            return;
        }
        break;
    default:
        break;
    }
    key_event(seen_ext, byte, !seen_break);
    seen_ext = false;
    seen_break = false;
}

void doom_kbd_reset(void)
{
    for (unsigned ext = 0; ext < 2u; ext++)
    {
        for (unsigned code = 0; code < SET2_SIZE; code++)
        {
            if (emitted[ext][code] != 0u)
            {
                key_event(ext != 0u, (uint8_t)code, false);
            }
        }
    }
    seen_ext = false;
    seen_break = false;
    pause_left = 0u;
}

int doom_kbd_pop(int *pressed, unsigned char *key)
{
    if (queue_head == queue_tail)
    {
        return 0;
    }
    uint16_t event = queue[queue_tail];

    queue_tail = (queue_tail + 1u) % QUEUE_SIZE;
    *pressed = event >> 8;
    *key = (unsigned char)(event & 0xFFu);
    return 1;
}

#ifdef DOOM_VIDEO
void doom_kbd_poll(void)
{
    uint32_t status = KBD_STATUS;
    bool lost = false;

    if ((status & KBD_RX_OVERFLOW) != 0u)
    {
        stats.overflows++;
        KBD_STATUS = KBD_RX_OVERFLOW;
        lost = true;
    }
    if ((status & KBD_RX_ERROR) != 0u)
    {
        stats.errors++;
        KBD_STATUS = KBD_RX_ERROR;
        lost = true;
    }
    for (uint32_t n = 0; n < KBD_POLL_MAX && (KBD_STATUS & KBD_RX_VALID) != 0u; n++)
    {
        doom_kbd_feed((uint8_t)(KBD_DATA & 0xFFu));
    }
    if (lost)
    {
        /* Um byte se perdeu: pode ter sido uma soltura, entao nenhuma tecla fica presa */
        doom_kbd_reset();
    }
}
#else
void doom_kbd_poll(void)
{
}
#endif

#ifndef DOOM_KBD_HOST_TEST
/*
 * Movimento por W A S D, uso por E e tiro por Espaco ou Ctrl: o jogo le key_up, key_down, key_strafeleft,
 * key_straferight, key_use e key_fire (m_controls.c) em G_BuildTiccmd, e so ali. Elas passam a ser o ASCII que o
 * decodificador ja entrega (as letras minusculas e ' ', que o Espaco e o Ctrl produzem), entao
 * os menus e a digitacao de texto (Y e N, nomes de save, cheats) recebem as letras certas sem conflito. As setas
 * esquerda e direita continuam girando (key_left e key_right) e todas as setas seguem navegando nos menus
 * (key_menu_*); cima e baixo ficam sem funcao no jogo. I_InitInput roda depois do M_LoadDefaults, entao um
 * default.cfg nao desfaz a troca.
 */
extern int key_up;
extern int key_down;
extern int key_strafeleft;
extern int key_straferight;
extern int key_use;
extern int key_fire;
extern void __real_I_InitInput(void);

/* saveStringEnter (m_menu.c) vale 1 enquanto se digita o nome do save; menuactive vem do doomstat.h */
extern int saveStringEnter;

/*
 * Nos menus W S A D e E navegam como as setas e o Enter (menu_translate), exceto na digitacao do nome de um save,
 * onde sao letras. As confirmacoes Y e N e os atalhos de letra dos menus (N, O, L, Q...) nao mudam.
 */
static int menu_nav_active(void)
{
    return menuactive != 0 && saveStringEnter == 0;
}

void __wrap_I_InitInput(void)
{
    doom_kbd_set_menu_hook(menu_nav_active);
    key_up = 'w';
    key_down = 's';
    key_strafeleft = 'a';
    key_straferight = 'd';
    key_use = 'e';
    key_fire = ' ';
    __real_I_InitInput();
}
#endif

const struct doom_kbd_stats *doom_kbd_get_stats(void)
{
    return &stats;
}

uint32_t doom_kbd_take_log(char *buf, uint32_t size)
{
    uint32_t n = (log_len < size - 1u) ? log_len : size - 1u;
    uint32_t lost = log_lost;

    for (uint32_t i = 0; i < n; i++)
    {
        buf[i] = log_buf[i];
    }
    buf[n] = '\0';
    log_len = 0u;
    log_lost = 0u;
    return lost;
}
