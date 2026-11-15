/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runtime minimo do DOOM sobre a picolibc, sem sistema de arquivos.
 *
 * Fornece: stdout/stderr (buffer de linha enviado ao console do Spike por
 * HTIF), `open` e afins que sempre falham (a WAD nao passa por eles, ver
 * doom_wad_mem.c), `_exit` com o resumo da execucao, e o `main` que monta
 * os argumentos de linha de comando.
 *
 * O HTIF e o mailbox do Spike (tohost e fromhost). Na placa, o console sera
 * a JTAG UART e este arquivo troca so as duas funcoes htif_*.
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

/* Mailbox HTIF, lido pelo Spike pelo nome do simbolo no ELF */
volatile uint64_t tohost __attribute__((section(".tohost")));
volatile uint64_t fromhost __attribute__((section(".fromhost")));

/* Argumentos do DOOM; -timedemo roda a demo sem relogio de parede */
#ifndef DOOM_ARGS
#define DOOM_ARGS "doom", "-iwad", "doom1.wad", "-timedemo", "demo1"
#endif

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
[[noreturn]] static void htif_exit(int code)
{
    tohost = ((uint64_t)(uint32_t)code << 1) | 1u;
    for (;;)
    {
    }
}

/* Buffer de linha do console */
static char line_buf[256];
static size_t line_len;

/* Descarrega o buffer de linha para o descritor fd */
static void line_flush(int fd)
{
    if (line_len != 0)
    {
        htif_write(fd, line_buf, line_len);
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
        line_flush(1);
    }
    return (unsigned char)c;
}

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

/* Encerra imprimindo o resumo que serve de gabarito (D0 e D1) */
void _exit(int code)
{
    uint64_t instret = doom_read_instret();
    uint32_t frames = doom_frames_drawn();
    uint64_t first = doom_instret_first_frame();
    uint64_t last = doom_instret_last_frame();
    uint64_t per_frame = (frames > 1) ? (last - first) / (frames - 1) : 0;

    printf("DOOM-D0: exit=%d frames=%lu instret=%llu instret_per_frame=%llu frame_crc32=%08lx\n",
           code, (unsigned long)frames, (unsigned long long)instret,
           (unsigned long long)per_frame, (unsigned long)doom_frame_crc32());
    line_flush(1);
    htif_exit(0);
}

int main(void)
{
    static char *args[] = { DOOM_ARGS };

    doomgeneric_Create((int)(sizeof(args) / sizeof(args[0])), args);
    for (;;)
    {
        doomgeneric_Tick();
    }
}
