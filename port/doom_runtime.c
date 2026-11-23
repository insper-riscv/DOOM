/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runtime minimo do DOOM sobre a picolibc, sem sistema de arquivos.
 *
 * Fornece: stdout/stderr, `open` e afins que sempre falham (a WAD nao passa por
 * eles, ver doom_wad_mem.c), `_exit` com o resumo da execucao, e o `main` que monta
 * os argumentos de linha de comando.
 *
 * O console tem dois destinos, escolhidos no build:
 *  - padrao (Spike): buffer de linha enviado ao console do Spike por HTIF, o mailbox
 *    do Spike (tohost e fromhost);
 *  - DOOM_CONSOLE_UART (placa): cada byte vai direto a JTAG UART da plataforma doom
 *    (slot 4, RV32_UART_BASE de memory_map.h), com o protocolo de runtime/stdio.c da
 *    plataforma: so envia quando um hospedeiro varreu a UART (bit 17 do estado) e
 *    espera um lugar na fila ate um limite.
 *
 * Ao terminar, _exit grava o bloco de resultado em DOOM_RESULT_ADDR (ver
 * doom_platform.h), que o `dump-ram` le, e imprime o resumo. Na placa o programa fica
 * repetindo o resumo na UART, para um console aberto depois o ver; no Spike encerra
 * a simulacao.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "doomgeneric.h"
#include "doom_platform.h"

#ifdef DOOM_MENU
#include "doom_menu.h"
#include "wad_table.h"
#include "doom_wad_src.h"
#endif

#ifdef DOOM_CONSOLE_UART
#include "memory_map.h"
#endif

#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
#include "doom_sd.h"
#endif

/* Argumentos do DOOM; -timedemo roda a demo sem relogio de parede */
#ifndef DOOM_ARGS
#ifdef DOOM_INTERACTIVE
/* Modo interativo (INTERACTIVE=1 no build.sh): titulo, menu e jogo, com o demo de atracao */
#define DOOM_ARGS "doom", "-iwad", "doom1.wad"
#else
#define DOOM_ARGS "doom", "-iwad", "doom1.wad", "-timedemo", "demo1"
#endif
#endif

#ifdef DOOM_CONSOLE_UART

/* Registradores de transmissao e de estado da JTAG UART (runtime/stdio.c da plataforma) */
#define UART_TXDATA ((volatile uint32_t *)RV32_UART_BASE)
#define UART_STATUS ((volatile uint32_t *)(RV32_UART_BASE + 8u))

/* Maximo de leituras do estado a espera de lugar na fila de transmissao */
#define UART_WAIT_POLLS 200000u

/* Envia um byte a UART quando ha um hospedeiro ouvindo, esperando lugar ate o limite */
static void uart_put(char c)
{
    uint32_t status = *UART_STATUS;

    if (((status >> 17) & 1u) == 0u)
    {
        return;
    }
    for (uint32_t polls = 0; (status & 0xFFu) == 0u; polls++)
    {
        if (polls == UART_WAIT_POLLS)
        {
            return;
        }
        status = *UART_STATUS;
    }
    *UART_TXDATA = (uint8_t)c;
}

/*
 * Copia circular do console na SDRAM (8 KiB em DOOM_LOG_ADDR; palavra 0 = indice de escrita, texto a partir do byte
 * 16), para ler pelo dump-ram o que o jogo disse quando ninguem tinha a UART aberta.
 */
#ifndef DOOM_LOG_ADDR
#define DOOM_LOG_ADDR 0x43BF0000u
#endif
#define DOOM_LOG_BYTES 8176u

static void log_put(char c)
{
    volatile uint32_t *head = (volatile uint32_t *)(uintptr_t)DOOM_LOG_ADDR;
    volatile char *text = (volatile char *)(uintptr_t)(DOOM_LOG_ADDR + 16u);
    uint32_t i = *head;

    text[i % DOOM_LOG_BYTES] = c;
    *head = i + 1u;
}

/* putc do stdout e do stderr: direto na UART */
static int console_putc(char c, FILE *f)
{
    (void)f;
    log_put(c);
    uart_put(c);
    return (unsigned char)c;
}

/* Nada a descarregar: a UART nao tem buffer de linha */
static void console_flush(void)
{
}

#else

/* Mailbox HTIF, lido pelo Spike pelo nome do simbolo no ELF */
volatile uint64_t tohost __attribute__((section(".tohost")));
volatile uint64_t fromhost __attribute__((section(".fromhost")));

/* Chamada de sistema write do proxy do Spike (numero da syscall do Linux/RISC-V) */
#define HTIF_SYS_WRITE 64

/* Envia len bytes ao descritor fd do host pelo proxy de syscalls do HTIF */
static void htif_write(int fd, const char *buf, size_t len)
{
    static volatile uint64_t magic_mem[8] __attribute__((aligned(64)));

    magic_mem[0] = HTIF_SYS_WRITE;
    magic_mem[1] = (uint64_t)fd;
    magic_mem[2] = (uint64_t)(uintptr_t)buf;
    magic_mem[3] = (uint64_t)len;
    __sync_synchronize();
    tohost = (uint64_t)(uintptr_t)magic_mem;
    while (fromhost == 0)
    {
    }
    fromhost = 0;
}

/* Termina a simulacao com o codigo dado (bit 0 de tohost marca o exit) */
[[noreturn, maybe_unused]] static void htif_exit(int code)
{
    tohost = ((uint64_t)(uint32_t)code << 1) | 1u;
    for (;;)
    {
    }
}

/* Buffer de linha do console */
static char line_buf[256];
static size_t line_len;

/* Descarrega o buffer de linha para o console do Spike */
static void console_flush(void)
{
    if (line_len != 0)
    {
        htif_write(1, line_buf, line_len);
        line_len = 0;
    }
}

/* putc do stdout e do stderr (os dois vao para o console, linha a linha) */
static int console_putc(char c, FILE *f)
{
    (void)f;
    line_buf[line_len++] = c;
    if (c == '\n' || line_len == sizeof(line_buf))
    {
        console_flush();
    }
    return (unsigned char)c;
}

#endif

static FILE console_file = FDEV_SETUP_STREAM(console_putc, NULL, NULL, _FDEV_SETUP_WRITE);
FILE *const stdout = &console_file;
FILE *const stderr = &console_file;
FILE *const stdin = &console_file;

/* Sem sistema de arquivos: todo open falha com ENOENT */
int open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    errno = ENOENT;
    return -1;
}

/*
 * Stubs que so existem para o link: fopen, fdopen e system da picolibc os
 * referenciam, mas nenhum caminho do -timedemo os alcanca com sucesso.
 */
int close(int fd) { (void)fd; errno = EBADF; return -1; }
ssize_t read(int fd, void *buf, size_t n) { (void)fd; (void)buf; (void)n; errno = EBADF; return -1; }
ssize_t write(int fd, const void *buf, size_t n) { (void)fd; (void)buf; (void)n; errno = EBADF; return -1; }
off_t lseek(int fd, off_t off, int whence) { (void)fd; (void)off; (void)whence; errno = EBADF; return -1; }
pid_t fork(void) { errno = ENOSYS; return -1; }
int execve(const char *p, char *const a[], char *const e[]) { (void)p; (void)a; (void)e; errno = ENOSYS; return -1; }
pid_t waitpid(pid_t p, int *s, int o) { (void)p; (void)s; (void)o; errno = ENOSYS; return -1; }

int mkdir(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = EROFS;
    return -1;
}

int remove(const char *path)
{
    (void)path;
    errno = ENOENT;
    return -1;
}

int rename(const char *from, const char *to)
{
    (void)from;
    (void)to;
    errno = ENOENT;
    return -1;
}

/* Bloco de resultado na SDRAM (doom_platform.h), lido pelo dump-ram */
#define RESULT ((volatile uint32_t *)(uintptr_t)DOOM_RESULT_ADDR)

/* Zera a palavra de magia, para um resultado de uma execucao anterior nao valer */
static void result_clear(void)
{
    RESULT[DOOM_RES_MAGIC] = 0;
}

/* Grava o bloco de resultado; a magia vai por ultimo, e marca o bloco completo */
static void result_write(int code, uint32_t crc, uint32_t frames, uint64_t instret,
                         uint64_t per_frame)
{
    uint64_t cycles = doom_read_cycle();

    RESULT[DOOM_RES_EXIT] = (uint32_t)code;
    RESULT[DOOM_RES_CRC32] = crc;
    RESULT[DOOM_RES_FRAMES] = frames;
    RESULT[DOOM_RES_INSTRET_LO] = (uint32_t)instret;
    RESULT[DOOM_RES_INSTRET_HI] = (uint32_t)(instret >> 32);
    RESULT[DOOM_RES_PER_FRAME] = (uint32_t)per_frame;
    RESULT[DOOM_RES_FB_ADDR] = (uint32_t)(uintptr_t)DG_ScreenBuffer;
    RESULT[DOOM_RES_MS] = DG_GetTicksMs();
    RESULT[DOOM_RES_CYCLE_LO] = (uint32_t)cycles;
    RESULT[DOOM_RES_CYCLE_HI] = (uint32_t)(cycles >> 32);
    __sync_synchronize();
    RESULT[DOOM_RES_MAGIC] = DOOM_RESULT_MAGIC;
}

/* Linha de resumo que serve de gabarito (D0 e D1) */
static void print_summary(int code, uint32_t crc, uint32_t frames, uint64_t instret,
                          uint64_t per_frame)
{
    printf("DOOM-D0: exit=%d frames=%lu instret=%llu instret_per_frame=%llu frame_crc32=%08lx\n",
           code, (unsigned long)frames, (unsigned long long)instret,
           (unsigned long long)per_frame, (unsigned long)crc);
    console_flush();
}

#ifdef DOOM_INTERACTIVE

/*
 * Reinicio limpo do programa (modo interativo). O I_Quit da doomgeneric sem ORIGCODE roda as funcoes de
 * encerramento (libera o buffer de video, para o som) e VOLTA, o que deixa o jogo rodando sobre estado destruido; e
 * o exit() de um I_Error caia no laco de resumo. Nos dois casos o programa volta ao inicio: restaura o .data, que o
 * jogo foi alterando (o endereco de carga e o de execucao sao o mesmo, entao nao ha copia pristina na imagem) a
 * partir da copia tirada no inicio do main, e salta para o _start, que zera o bss, refaz o TLS e chama o main. A
 * WAD e o binario ja estao na SDRAM, nada e recarregado; o VIDEO e o KBD seguem ligados e mostram o ultimo quadro
 * ate o primeiro quadro novo.
 */
extern char __data_start[];
extern char __data_end[];
extern void _start(void);

/* Copia do .data na SDRAM livre (DOOM_DATA_BACKUP_ADDR, scripts/layout.sh) */
#define DATA_BACKUP ((volatile uint32_t *)(uintptr_t)DOOM_DATA_BACKUP_ADDR)

/* Guarda o .data inicial; chamada no inicio do main, antes de o jogo o alterar */
static void data_snapshot(void)
{
    const volatile uint32_t *src = (const volatile uint32_t *)(uintptr_t)__data_start;
    uint32_t words = (uint32_t)((uintptr_t)__data_end - (uintptr_t)__data_start) / 4u;

    for (uint32_t i = 0; i < words; i++)
    {
        DATA_BACKUP[i] = src[i];
    }
}

/* Espera ms milissegundos pelo relogio do jogo */
static void wait_ms(uint32_t ms)
{
    uint32_t start = DG_GetTicksMs();

    while ((uint32_t)(DG_GetTicksMs() - start) < ms)
    {
    }
}

/* Restaura o .data e salta para o _start; nenhuma chamada de biblioteca depois da restauracao */
[[noreturn]] static void restart_program(void)
{
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)__data_start;
    uint32_t words = (uint32_t)((uintptr_t)__data_end - (uintptr_t)__data_start) / 4u;

    for (uint32_t i = 0; i < words; i++)
    {
        dst[i] = DATA_BACKUP[i];
    }
    _start();
    for (;;)
    {
    }
}

/* Inatividade: o jogo ficou idle_ms sem nenhum evento de teclado (port/doom_video.c) e recomeca no titulo */
void doom_restart_idle(uint32_t idle_ms)
{
    if (idle_ms % 60000u < 1000u)
    {
        printf("[DOOM] Inatividade de %lu min: reiniciando o jogo\n", (unsigned long)(idle_ms / 60000u));
    }
    else
    {
        printf("[DOOM] Inatividade de %lu s: reiniciando o jogo\n", (unsigned long)(idle_ms / 1000u));
    }
    console_flush();
    restart_program();
}

/* Quit Game do menu: o jogo recomeca na tela de titulo, sem recarregar nada */
void __wrap_I_Quit(void)
{
#ifdef DOOM_MENU
    printf("[DOOM] Quit Game: reiniciando, volta ao menu de escolha do jogo\n");
#else
    printf("[DOOM] Quit Game: reiniciando o jogo (titulo e menu)\n");
#endif
    console_flush();
    restart_program();
}

#else

/* Sem modo interativo o I_Quit e o original */
extern void __real_I_Quit(void);

void __wrap_I_Quit(void)
{
    __real_I_Quit();
}

#endif

/* Encerra gravando o resultado e imprimindo o resumo */
void _exit(int code)
{
    uint64_t instret = doom_read_instret();
    uint32_t frames = doom_frames_drawn();
    uint64_t first = doom_instret_first_frame();
    uint64_t last = doom_instret_last_frame();
    uint64_t per_frame = (frames > 1) ? (last - first) / (frames - 1) : 0;
    uint32_t crc = doom_frame_crc32();

    result_write(code, crc, frames, instret, per_frame);
    print_summary(code, crc, frames, instret, per_frame);
#ifdef DOOM_INTERACTIVE
    /* Fim por I_Error ou exit: deixa 3 s para ler a mensagem na UART e recomeca o jogo */
    printf("[DOOM] fim do programa (codigo %d): reiniciando em 3 s\n", code);
    console_flush();
    wait_ms(3000u);
    restart_program();
#elif defined(DOOM_CONSOLE_UART)
    /* Fica repetindo o resumo a cada 2 s, para um console aberto depois o ver */
    for (;;)
    {
        uint32_t start = DG_GetTicksMs();

        while ((uint32_t)(DG_GetTicksMs() - start) < 2000u)
        {
        }
        print_summary(code, crc, frames, instret, per_frame);
    }
#else
    htif_exit(0);
#endif
}

#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)

/*
 * Carga das WADs pelo cartao microSD (port/doom_sd.c), no inicio do main e antes do menu. DOOM_SD_LOAD copia a imagem
 * do cartao para a SDRAM (DOOM_WAD_ADDR, DOOM_WAD_SIZE do build); DOOM_SD_DEMAND le o indice do cartao e deixa o
 * backend de WAD ler os blocos sob demanda. Sem cartao, ou com um cartao que nao e a imagem deste build, o jogo
 * segue pelo caminho da JTAG (a WAD ja na SDRAM por `load-ram --wad`), como antes.
 */
static void sd_boot(void)
{
    uint32_t t0 = DG_GetTicksMs();
    int status = doom_sd_init();

    if (status == DOOM_SD_OK)
    {
#ifdef DOOM_SD_DEMAND
        status = doom_sd_load_index();
#else
        status = doom_sd_load_image(DOOM_WAD_ADDR, DOOM_WAD_SIZE);
#endif
    }
    if (status == DOOM_SD_OK)
    {
        printf("[SD] pronto em %u ms\n", (unsigned)(DG_GetTicksMs() - t0));
    }
    else
    {
        printf("[SD] %s: usando a WAD que ja esta na SDRAM (JTAG)\n", doom_sd_status_text(status));
#ifdef DOOM_SD_DEMAND
        doom_sd_use_static_table();
#ifdef DOOM_SD_ONLY
        /* ONDEMAND=1: a tabela tem deslocamentos no cartao, nao enderecos da SDRAM; sem cartao nao ha jogo */
#ifdef DOOM_SD_FATFS
        printf("[SD] build so com cartao (SDFS=fatfs): insira um FAT32 com DOOM.WAD, DOOM2.WAD, PLUTONIA.WAD ou TNT.WAD e reinicie\n");
#else
        printf("[SD] build so com cartao (ONDEMAND=1): insira o cartao gravado pelo sdwrite e reinicie\n");
#endif
        console_flush();
        for (;;)
        {
        }
#endif
#endif
    }
    console_flush();
}

#endif

#ifdef DOOM_MENU

/*
 * Com o menu (MENU=1 no build.sh): mostra a escolha do jogo e monta os argumentos do DOOM a partir dela. Um jogo sai
 * como `doom -iwad doom2.wad`; uma fase do Master Levels, como `doom -iwad doom2.wad -file attack.wad -warp 1`, que e
 * o que o DMLOAD do pacote faz. A escolha vale ate o jogo reiniciar (Quit Game, fim por erro ou inatividade), que
 * volta a este ponto e ao menu.
 */
int main(void)
{
    static char *args[10];
    static char warp_text[8];
    struct doom_menu_choice choice;
    int argc = 0;

    data_snapshot();
    result_clear();
    doom_measure_init();
#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
    sd_boot();
#endif
#ifdef DOOM_MENU_AUTO
#ifndef DOOM_MENU_AUTO_GAME
#define DOOM_MENU_AUTO_GAME 0
#endif
#ifndef DOOM_MENU_AUTO_DEMO
#define DOOM_MENU_AUTO_DEMO "demo1"
#endif
    /* Medicao pelo cartao (MENU_AUTO=1): sem escolha, abre o primeiro jogo e roda o demo1 sem esperar o relogio */
    choice.iwad = doom_menu_games[DOOM_MENU_AUTO_GAME].iwad;
    choice.pwad = -1;
    choice.map = 0;
#else
    doom_menu_run(&choice);
#endif
    args[argc++] = (char *)"doom";
    args[argc++] = (char *)"-iwad";
    args[argc++] = (char *)doom_wad_table[choice.iwad].name;
    if (choice.pwad >= 0)
    {
        snprintf(warp_text, sizeof(warp_text), "%d", choice.map);
        args[argc++] = (char *)"-file";
        args[argc++] = (char *)doom_wad_table[choice.pwad].name;
        args[argc++] = (char *)"-warp";
        args[argc++] = warp_text;
    }
#ifdef DOOM_MENU_AUTO
    args[argc++] = (char *)"-timedemo";
    args[argc++] = (char *)DOOM_MENU_AUTO_DEMO;
#endif
    printf("[DOOM] menu: %s%s%s\n", doom_wad_table[choice.iwad].name, choice.pwad >= 0 ? " + " : "",
           choice.pwad >= 0 ? doom_wad_table[choice.pwad].name : "");
    console_flush();
#ifdef DOOM_MENU_STOP
    /* Teste no Spike: termina logo depois da escolha, para conferir o menu sem rodar o jogo */
    htif_exit(0);
#endif
    doomgeneric_Create(argc, args);
    for (;;)
    {
        doomgeneric_Tick();
    }
}

#else

int main(void)
{
    static char *args[] = { DOOM_ARGS };

#ifdef DOOM_INTERACTIVE
    data_snapshot();
#endif
    result_clear();
    doom_measure_init();
#if defined(DOOM_SD_LOAD) || defined(DOOM_SD_DEMAND)
    sd_boot();
#endif
    doomgeneric_Create((int)(sizeof(args) / sizeof(args[0])), args);
    for (;;)
    {
        doomgeneric_Tick();
    }
}

#endif /* DOOM_MENU */
