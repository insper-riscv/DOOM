// SPDX-License-Identifier: Apache-2.0
// Filtro em fluxo: le o log de commits do Spike (stdin, --log-commits) e escreve um trace binario
// compacto (busca, load, store) das janelas de quadros pedidas, mais as contagens de classes de
// instrucao (laco, desvio tomado, mul/div, load-use) por janela em stderr.
//
// Uso: filter DRAWFRAME_PC [-s] FIRST:LAST[:arquivo.bin] ...
//   DRAWFRAME_PC  endereco (hex) de DG_DrawFrame; entrar nele conta o quadro N (como o Zihpm da placa)
//   FIRST:LAST    grava as instrucoes entre a entrada do quadro FIRST e a do quadro LAST (exclusive o LAST)
//   arquivo.bin   destino do trace dessa janela ("-" = stdout); sem arquivo so conta
// O filtro termina (e fecha o pipe do Spike) quando passa do maior LAST.
//
// Registro do trace: um uint32. bits 28..26 = tipo, bits 25..0 = deslocamento na SDRAM (0x40000000).
//   0 busca, 1 load 1 B, 2 load 2 B, 3 load 4 B, 4 store 1 B, 5 store 2 B, 6 store 4 B, 7 marca
//   (marca: bits 25..0 = numero do quadro cuja entrada foi vista).
// Enderecos fora de 0x40000000..0x43FFFFFF (HTIF, etc.) sao descartados dos acessos de dado.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXW 16
typedef struct {
    uint32_t first, last;
    FILE *out;
    uint64_t instr, loads, stores, branches_taken, jumps, mul, div, loaduse, branches, ld[3], st[3];
} win_t;

static int hexv(const char *p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        char c = p[i];
        v = (v << 4) | (uint32_t)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return (int)v;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "uso: filter DRAWFRAME_PC FIRST:LAST[:arq] ...\n");
        return 2;
    }
    uint32_t dpc = (uint32_t)strtoul(argv[1], NULL, 16);
    win_t w[MAXW];
    int nw = 0;
    uint32_t maxlast = 0;
    for (int i = 2; i < argc && nw < MAXW; i++) {
        char *s = argv[i];
        char *c1 = strchr(s, ':');
        if (!c1) continue;
        *c1 = 0;
        char *c2 = strchr(c1 + 1, ':');
        if (c2) *c2 = 0;
        memset(&w[nw], 0, sizeof(win_t));
        w[nw].first = (uint32_t)atoi(s);
        w[nw].last = (uint32_t)atoi(c1 + 1);
        if (c2) w[nw].out = strcmp(c2 + 1, "-") == 0 ? stdout : fopen(c2 + 1, "wb");
        if (w[nw].last > maxlast) maxlast = w[nw].last;
        nw++;
    }
    static char line[512];
    uint32_t frame = 0;
    uint32_t prev_pc = 0, prev_inst = 0;
    int prev_valid = 0, prev_load_rd = 0;
    static uint32_t buf[1 << 16];
    (void)buf;
    uint64_t total = 0;
    while (fgets(line, sizeof line, stdin)) {
        if (strncmp(line, "core   0: 3 0x", 14) != 0) continue;
        uint32_t pc = (uint32_t)hexv(line + 14, 8);
        uint32_t ins = (uint32_t)hexv(line + 26, 8);
        total++;
        if (pc == dpc) {
            frame++;
            for (int k = 0; k < nw; k++)
                if (w[k].out && frame >= w[k].first && frame <= w[k].last) {
                    uint32_t m = (7u << 26) | (frame & 0x3FFFFFFu);
                    fwrite(&m, 4, 1, w[k].out);
                }
            if (frame >= maxlast) break;
        }
        // ---- classificacao da instrucao anterior (desvio tomado = pc atual != prev_pc + 4)
        int taken = prev_valid && pc != prev_pc + 4;
        uint32_t op = ins & 0x7f, rd = (ins >> 7) & 31, rs1 = (ins >> 15) & 31, rs2 = (ins >> 20) & 31;
        uint32_t f3 = (ins >> 12) & 7, f7 = ins >> 25;
        // usa rs1/rs2?
        int u1 = !(op == 0x37 || op == 0x17 || op == 0x6f || op == 0x73 /*csr:aprox*/);
        int u2 = (op == 0x33 || op == 0x63 || op == 0x23);
        int lu = prev_load_rd && ((u1 && rs1 == (uint32_t)prev_load_rd) || (u2 && rs2 == (uint32_t)prev_load_rd));
        uint32_t da = 0;
        int dk = -1;  // 1..6
        if (op == 0x03 || op == 0x23) {
            char *m = strstr(line + 34, "mem 0x");
            if (m) {
                da = (uint32_t)strtoul(m + 4, NULL, 16);
                int sz = (f3 & 3) == 0 ? 1 : (f3 & 3) == 1 ? 2 : 4;
                int si = sz == 1 ? 0 : sz == 2 ? 1 : 2;
                dk = (op == 0x03 ? 1 : 4) + si;
            }
        }
        for (int k = 0; k < nw; k++) {
            if (!(frame >= w[k].first && frame < w[k].last)) continue;
            win_t *x = &w[k];
            x->instr++;
            if (taken) x->branches_taken++;
            if (lu) x->loaduse++;
            if (op == 0x63) x->branches++;
            if (op == 0x6f || op == 0x67) x->jumps++;
            if (op == 0x33 && f7 == 1) {
                if (f3 < 4) x->mul++; else x->div++;
            }
            if (dk >= 1 && dk <= 3) { x->loads++; x->ld[dk - 1]++; }
            if (dk >= 4) { x->stores++; x->st[dk - 4]++; }
            if (x->out) {
                uint32_t r = pc & 0x3FFFFFFu;
                fwrite(&r, 4, 1, x->out);
                if (dk >= 1 && (da >> 26) == 0x10) {
                    r = ((uint32_t)dk << 26) | (da & 0x3FFFFFFu);
                    fwrite(&r, 4, 1, x->out);
                }
            }
        }
        prev_pc = pc;
        prev_inst = ins;
        prev_valid = 1;
        prev_load_rd = (op == 0x03 && rd) ? (int)rd : 0;
    }
    (void)prev_inst;
    for (int k = 0; k < nw; k++) {
        win_t *x = &w[k];
        if (x->out && x->out != stdout) fclose(x->out);
        fprintf(stderr,
                "janela %u..%u: instr %llu loads %llu (b %llu h %llu w %llu) stores %llu (b %llu h %llu w %llu) "
                "tomados %llu desvios %llu jumps %llu mul %llu div %llu loaduse %llu\n",
                x->first, x->last, (unsigned long long)x->instr, (unsigned long long)x->loads,
                (unsigned long long)x->ld[0], (unsigned long long)x->ld[1], (unsigned long long)x->ld[2],
                (unsigned long long)x->stores, (unsigned long long)x->st[0], (unsigned long long)x->st[1],
                (unsigned long long)x->st[2], (unsigned long long)x->branches_taken,
                (unsigned long long)x->branches, (unsigned long long)x->jumps, (unsigned long long)x->mul,
                (unsigned long long)x->div, (unsigned long long)x->loaduse);
    }
    fprintf(stderr, "linhas lidas %llu, quadro final %u\n", (unsigned long long)total, frame);
    return 0;
}
