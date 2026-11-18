/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Teclado PS/2 (periferico KBD da plataforma doom): decodificador do conjunto 2 de scancodes
 * para as teclas do doomkeys.h e fila de eventos.
 *
 * O decodificador (doom_kbd_feed) nao toca o hardware, e por isso roda no computador em
 * scripts/test_kbd.c. A leitura do KBD (doom_kbd_poll) existe so quando o build define
 * DOOM_VIDEO (placa).
 */

#ifndef DOOM_KBD_H
#define DOOM_KBD_H

#include <stdint.h>

/* Estatisticas do teclado desde o inicio */
struct doom_kbd_stats
{
    uint32_t bytes;     /* bytes lidos do KBD */
    uint32_t pressed;   /* eventos de tecla pressionada entregues ao jogo */
    uint32_t released;  /* eventos de tecla solta entregues ao jogo */
    uint32_t overflows; /* estouros da fila de 16 bytes do KBD */
    uint32_t errors;    /* erros de quadro ou paridade */
    uint32_t dropped;   /* eventos descartados porque a fila de software estava cheia */
};

/* Entrega ao decodificador um byte do teclado (conjunto 2) */
void doom_kbd_feed(uint8_t byte);

/* Solta todas as teclas pressionadas e zera o decodificador (apos um byte perdido) */
void doom_kbd_reset(void);

/* Retira o evento mais antigo da fila: 1 com *pressed e *key preenchidos, ou 0 se vazia */
int doom_kbd_pop(int *pressed, unsigned char *key);

/* Le o KBD ate esvaziar a fila de hardware (no-op sem DOOM_VIDEO) */
void doom_kbd_poll(void);

/* Estatisticas (leitura) */
const struct doom_kbd_stats *doom_kbd_get_stats(void);

/*
 * Copia para buf (de size bytes, com o terminador) o registro dos eventos entregues desde a
 * ultima chamada, como "+75e -75e +29" (+ pressionada, - solta, codigo do conjunto 2 em hex,
 * "e" se estendido), e o zera. Devolve a quantidade de eventos que nao couberam.
 */
uint32_t doom_kbd_take_log(char *buf, uint32_t size);

#endif
