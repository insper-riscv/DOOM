/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Teste no computador do decodificador de scancodes (port/doom_kbd.c, sem DOOM_VIDEO):
 *   gcc -std=gnu17 -Wall -Wextra -Werror -DDOOM_KBD_HOST_TEST -Idoomgeneric/doomgeneric -Iport scripts/test_kbd.c port/doom_kbd.c -o test_kbd && ./test_kbd
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomkeys.h"
#include "doom_kbd.h"

static int failures;
static int menu_active;

static int menu_hook(void)
{
    return menu_active;
}

static void feed(const unsigned char *bytes, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
    {
        doom_kbd_feed(bytes[i]);
    }
}

#define FEED(...) feed((const unsigned char[]){ __VA_ARGS__ }, sizeof((const unsigned char[]){ __VA_ARGS__ }))

/* Confere o proximo evento da fila */
static void expect(int pressed, unsigned key, const char *what)
{
    int p = -1;
    unsigned char k = 0;

    if (!doom_kbd_pop(&p, &k) || p != pressed || k != key)
    {
        printf("FALHA %s: esperava %s %u, veio %s %u\n", what, pressed ? "+" : "-", key,
               p == 1 ? "+" : p == 0 ? "-" : "(nada)", k);
        failures++;
    }
}

static void expect_empty(const char *what)
{
    int p;
    unsigned char k;

    if (doom_kbd_pop(&p, &k))
    {
        printf("FALHA %s: sobrou o evento %d %u\n", what, p, k);
        failures++;
    }
}

int main(void)
{
    /* letra: make, repeticao, break */
    FEED(0x1C);
    FEED(0x1C, 0x1C, 0x1C);
    FEED(0xF0, 0x1C);
    expect(1, 'a', "A make");
    expect(0, 'a', "A break");
    expect_empty("repeticao");

    /* respostas do teclado */
    FEED(0xFA, 0xAA, 0x00, 0xFE, 0xFF);
    expect_empty("FA AA 00 FE FF");

    /* setas estendidas */
    FEED(0xE0, 0x75, 0xE0, 0x72, 0xE0, 0x6B, 0xE0, 0x74);
    FEED(0xE0, 0xF0, 0x75, 0xE0, 0xF0, 0x72, 0xE0, 0xF0, 0x6B, 0xE0, 0xF0, 0x74);
    expect(1, KEY_UPARROW, "cima");
    expect(1, KEY_DOWNARROW, "baixo");
    expect(1, KEY_LEFTARROW, "esquerda");
    expect(1, KEY_RIGHTARROW, "direita");
    expect(0, KEY_UPARROW, "solta cima");
    expect(0, KEY_DOWNARROW, "solta baixo");
    expect(0, KEY_LEFTARROW, "solta esquerda");
    expect(0, KEY_RIGHTARROW, "solta direita");
    expect_empty("setas");

    /* Shift falso do PrtSc/Insert nao vira Shift: E0 12 E0 75 ... E0 F0 75 E0 F0 12 */
    FEED(0xE0, 0x12, 0xE0, 0x75, 0xE0, 0xF0, 0x75, 0xE0, 0xF0, 0x12);
    expect(1, KEY_UPARROW, "seta com shift falso");
    expect(0, KEY_UPARROW, "solta seta com shift falso");
    expect_empty("shift falso");

    /* as setas do teclado numerico sem prefixo (Num Lock ligado) nao sao teclas de jogo */
    FEED(0x75, 0xF0, 0x75);
    expect_empty("KP 8");

    /* Ctrl esquerdo e direito: duas teclas fisicas, soltar uma nao solta a outra por engano */
    FEED(0x14, 0xE0, 0x14, 0xF0, 0x14, 0xE0, 0xF0, 0x14);
    expect(1, ' ', "ctrl esquerdo");
    expect(1, ' ', "ctrl direito");
    expect(0, ' ', "solta ctrl esquerdo");
    expect(0, ' ', "solta ctrl direito");
    expect_empty("ctrl");

    /* Shift, Alt, Espaco, Enter, Esc, Tab, Backspace */
    FEED(0x12, 0x59, 0x11, 0xE0, 0x11, 0x29, 0x5A, 0xE0, 0x5A, 0x76, 0x0D, 0x66);
    expect(1, KEY_RSHIFT, "shift esquerdo");
    expect(1, KEY_RSHIFT, "shift direito");
    expect(1, KEY_LALT, "alt");
    expect(1, KEY_LALT, "alt direito");
    expect(1, ' ', "espaco");
    expect(1, KEY_ENTER, "enter");
    expect(1, KEY_ENTER, "enter do teclado numerico");
    expect(1, KEY_ESCAPE, "esc");
    expect(1, KEY_TAB, "tab");
    expect(1, KEY_BACKSPACE, "backspace");
    expect_empty("acoes");
    doom_kbd_reset();
    for (int i = 0; i < 10; i++)
    {
        int p;
        unsigned char k;

        if (!doom_kbd_pop(&p, &k) || p != 0)
        {
            printf("FALHA reset: solta %d ausente\n", i);
            failures++;
        }
    }
    expect_empty("reset");

    /* F1 a F12 */
    static const unsigned char fcodes[12] = { 0x05, 0x06, 0x04, 0x0C, 0x03, 0x0B, 0x83, 0x0A, 0x01, 0x09, 0x78, 0x07 };

    for (unsigned i = 0; i < 12; i++)
    {
        FEED(fcodes[i], 0xF0, fcodes[i]);
        expect(1, i < 10 ? KEY_F1 + i : (i == 10 ? KEY_F11 : KEY_F12), "F");
        expect(0, i < 10 ? KEY_F1 + i : (i == 10 ? KEY_F11 : KEY_F12), "F solta");
    }
    expect_empty("F1 a F12");

    /* letras e digitos: todas as 26 letras, 0 a 9, '-', '=', virgula e ponto */
    static const struct { unsigned char code; unsigned char key; } table[] = {
        { 0x1C, 'a' }, { 0x32, 'b' }, { 0x21, 'c' }, { 0x23, 'd' }, { 0x24, 'e' }, { 0x2B, 'f' }, { 0x34, 'g' },
        { 0x33, 'h' }, { 0x43, 'i' }, { 0x3B, 'j' }, { 0x42, 'k' }, { 0x4B, 'l' }, { 0x3A, 'm' }, { 0x31, 'n' },
        { 0x44, 'o' }, { 0x4D, 'p' }, { 0x15, 'q' }, { 0x2D, 'r' }, { 0x1B, 's' }, { 0x2C, 't' }, { 0x3C, 'u' },
        { 0x2A, 'v' }, { 0x1D, 'w' }, { 0x22, 'x' }, { 0x35, 'y' }, { 0x1A, 'z' }, { 0x45, '0' }, { 0x16, '1' },
        { 0x1E, '2' }, { 0x26, '3' }, { 0x25, '4' }, { 0x2E, '5' }, { 0x36, '6' }, { 0x3D, '7' }, { 0x3E, '8' },
        { 0x46, '9' }, { 0x4E, KEY_MINUS }, { 0x55, KEY_EQUALS }, { 0x41, ',' }, { 0x49, '.' },
    };

    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        FEED(table[i].code, 0xF0, table[i].code);
        expect(1, table[i].key, "tecla de texto");
        expect(0, table[i].key, "tecla de texto solta");
    }
    expect_empty("texto");

    /* Pause: E1 14 77 E1 F0 14 F0 77 nao gera tecla e nao suja o estado */
    FEED(0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77);
    FEED(0x1C, 0xF0, 0x1C);
    expect(1, 'a', "A depois do Pause");
    expect(0, 'a', "A solta depois do Pause");
    expect_empty("Pause");

    /* solta sem pressionar nao gera evento */
    FEED(0xF0, 0x1C);
    expect_empty("solta sem make");

    /* a fila de software de 64 entradas descarta o excesso e conta */
    for (int i = 0; i < 40; i++)
    {
        FEED(0x1C, 0xF0, 0x1C);
    }
    int count = 0;
    int p;
    unsigned char k;

    while (doom_kbd_pop(&p, &k))
    {
        count++;
    }
    if (count != 63 || doom_kbd_get_stats()->dropped != 80u - 63u)
    {
        printf("FALHA fila: %d eventos, %u descartados\n", count, doom_kbd_get_stats()->dropped);
        failures++;
    }

    /* menu ativo: W S A D e E viram setas e Enter; as outras letras (Y, N, O) ficam */
    menu_active = 1;
    doom_kbd_set_menu_hook(menu_hook);
    FEED(0x1D, 0xF0, 0x1D, 0x1B, 0xF0, 0x1B, 0x1C, 0xF0, 0x1C, 0x23, 0xF0, 0x23, 0x24, 0xF0, 0x24);
    FEED(0x35, 0xF0, 0x35, 0x31, 0xF0, 0x31, 0x44, 0xF0, 0x44);
    static const unsigned char menu_keys[] = { KEY_UPARROW, KEY_DOWNARROW, KEY_LEFTARROW, KEY_RIGHTARROW, KEY_ENTER, 'y', 'n', 'o' };

    for (unsigned i = 0; i < sizeof(menu_keys); i++)
    {
        expect(1, menu_keys[i], "tecla no menu");
        expect(0, menu_keys[i], "solta no menu");
    }
    expect_empty("menu");

    /* digitacao de texto no menu (nome de save): as letras normais, WASD inclusive */
    menu_active = 0;
    FEED(0x1D, 0xF0, 0x1D, 0x1C, 0xF0, 0x1C, 0x1B, 0xF0, 0x1B, 0x23, 0xF0, 0x23, 0x24, 0xF0, 0x24);
    static const unsigned char text_keys[] = { 'w', 'a', 's', 'd', 'e' };

    for (unsigned i = 0; i < sizeof(text_keys); i++)
    {
        expect(1, text_keys[i], "letra no texto");
        expect(0, text_keys[i], "solta no texto");
    }
    expect_empty("texto do save");

    /* o menu abre com a tecla pressionada: a soltura entrega a tecla do make, nada fica preso */
    FEED(0x1D);
    menu_active = 1;
    FEED(0xF0, 0x1D);
    expect(1, 'w', "w pressionada no jogo");
    expect(0, 'w', "w solta com o menu aberto");
    FEED(0x1D);
    menu_active = 0;
    FEED(0xF0, 0x1D);
    expect(1, KEY_UPARROW, "w pressionada no menu");
    expect(0, KEY_UPARROW, "w solta com o menu fechado");
    expect_empty("troca de contexto");
    doom_kbd_set_menu_hook(NULL);

    /* registro de eventos */
    char log[64];

    doom_kbd_take_log(log, sizeof(log));
    FEED(0xE0, 0x75, 0xE0, 0xF0, 0x75);
    uint32_t lost = doom_kbd_take_log(log, sizeof(log));
    if (strcmp(log, "+75e -75e ") != 0 || lost != 0u)
    {
        printf("FALHA log: \"%s\"\n", log);
        failures++;
    }
    while (doom_kbd_pop(&p, &k))
    {
    }

    if (failures == 0)
    {
        printf("test_kbd: tudo certo (%u bytes, %u pressionadas, %u soltas)\n", doom_kbd_get_stats()->bytes,
               doom_kbd_get_stats()->pressed, doom_kbd_get_stats()->released);
        return 0;
    }
    return 1;
}
