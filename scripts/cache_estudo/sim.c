// SPDX-License-Identifier: Apache-2.0
// Simulador de caches + espera por SDRAM do Insper-RISC-V (DE0-CV) sobre traces do DOOM no Spike.
//
// Entrada: traces binarios do filter (registro uint32: tipo<<26 | deslocamento na SDRAM).
// Modelo (ver RELATORIO.md): cada configuracao tem icache, dcache (linha, associatividade LRU,
// politica de escrita), regioes cacheaveis, memoria interna fixa (paginas "quentes"), buffer de
// escrita opcional. O tempo e contado em ciclos do nucleo; a SDRAM e um recurso unico em ordem.
//
// Uso: sim [-p perfil.tsv] [-t treino.bin] [-w quadros_aquecimento] [-o saida.tsv] -c "cfg" ... arq.bin ...
//   cfg: nome:chave=valor:...   (chaves abaixo em parse_cfg)
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CM_LO 0xb7b8u  /* cmap_to_fb: 0x4000b7b8..0x4000b8fc (nm do doom.elf do layout) */
#define CM_HI 0xb8fcu
enum { K_FETCH = 0, K_LD1, K_LD2, K_LD4, K_ST1, K_ST2, K_ST4, K_MARK };
enum { R_TEXT, R_RODATA, R_DATA, R_BSS, R_WAD, R_FB, R_HEAP, R_STACK, R_LAST, R_OTHER, NREG };
static const char *REGN[NREG] = {"text", "rodata", "data", "bss", "wad", "fb", "heap", "stack", "ultima_pag", "outro"};

static int region_of(uint32_t o) {
    if (o < 0x3b0f0) return R_TEXT;
    if (o < 0x54340) return R_RODATA;
    if (o < 0x62d40) return R_DATA;
    if (o < 0x9e698 + 0x1000) return R_BSS;
    if (o >= 0x100000 && o < 0x580000) return R_WAD;
    if (o >= 0x580000 && o < 0x5C0000) return R_FB;
    if (o >= 0x5C0000 && o < 0x3FEF000) return R_HEAP;
    if (o >= 0x3FEF000 && o < 0x3FFF000) return R_STACK;
    if (o >= 0x3FFF000) return R_LAST;
    return R_OTHER;
}

typedef struct {
    int size_kib, line, assoc;
    int sets, lshift, sbits;
    uint32_t *tag;  // 0xFFFFFFFF = invalida
    uint32_t *stamp;
    uint8_t *dirty;
    uint32_t clk;
} cache_t;

static void cache_init(cache_t *c, int size_kib, int line, int assoc) {
    memset(c, 0, sizeof *c);
    c->size_kib = size_kib;
    c->line = line;
    c->assoc = assoc;
    c->sets = size_kib * 1024 / (line * assoc);
    c->lshift = __builtin_ctz((unsigned)line);
    c->sbits = __builtin_ctz((unsigned)c->sets);
    c->tag = malloc(sizeof(uint32_t) * (size_t)c->sets * assoc);
    c->stamp = calloc((size_t)c->sets * assoc, sizeof(uint32_t));
    c->dirty = calloc((size_t)c->sets * assoc, 1);
    memset(c->tag, 0xFF, sizeof(uint32_t) * (size_t)c->sets * assoc);
}
static void cache_reset(cache_t *c) {
    memset(c->tag, 0xFF, sizeof(uint32_t) * (size_t)c->sets * c->assoc);
    memset(c->stamp, 0, sizeof(uint32_t) * (size_t)c->sets * c->assoc);
    memset(c->dirty, 0, (size_t)c->sets * c->assoc);
    c->clk = 0;
}
// devolve indice do way em acerto, ou -1
static inline int cache_find(cache_t *c, uint32_t a, uint32_t *set, uint32_t *tg) {
    uint32_t blk = a >> c->lshift;
    *set = blk & (uint32_t)(c->sets - 1);
    *tg = blk >> c->sbits;
    size_t base = (size_t)*set * c->assoc;
    for (int w = 0; w < c->assoc; w++)
        if (c->tag[base + w] == *tg) return (int)(base + w);
    return -1;
}
// escolhe a vitima (invalida primeiro, senao LRU)
static inline size_t cache_victim(cache_t *c, uint32_t set) {
    size_t base = (size_t)set * c->assoc, v = base;
    for (int w = 0; w < c->assoc; w++) {
        if (c->tag[base + w] == 0xFFFFFFFFu) return base + w;
        if (c->stamp[base + w] < c->stamp[v]) v = base + w;
    }
    return v;
}

typedef enum { P_WT = 0, P_WB = 1 } pol_t;

typedef struct {
    char name[64];
    double f_mhz;
    int ic_kib, ic_line, ic_assoc;
    int dc_kib, dc_line, dc_assoc;
    pol_t pol;
    int fb_cached, wad_cached;  // 1 = a regiao passa pela dcache
    int wbuf;                   // entradas do buffer de escrita (0 = nenhum; escritas bloqueiam)
    int scr_pages;              // paginas de 4 KiB na memoria interna (escolhidas pelo treino)
    double srv_word_ns, srv_line_ns;
    double fill_scale;          // fator de calibracao da latencia de falha
    double store_scale;
    // ---- estado
    cache_t ic, dc;
    double t, bus_free;
    double q_done[16];
    int q_n, q_last_words;
    double q_last_start, q_last_srv;
    uint32_t q_last_line;
    // ---- contadores
    uint64_t instr, ifill, dfill, dwb, ld, st, dhit_ld, dhit_st, st_mem, scr_acc, unc_ld, unc_st;
    double w_ifetch, w_dload_hit, w_dload_fill, w_dstore, w_evict, w_wbuf_full, w_dunc;
    uint64_t reg_ld[NREG], reg_st[NREG], reg_dmiss[NREG], reg_dfill_lines;
    int cm, in_cm, cm_done;  // cm=1: cenario CMAP256 (sem cmap_to_fb)
    double vst;              // espera de um store na janela do video (periferico)
    double t_start, nm, hw;  // hw: espera de um acerto da dcache (S_IDLE, S_LOOK, S_DONE: 2 ciclos sem o_ready)
    int counting;
} cfg_t;

// ---- parametros de tempo (ciclos do nucleo) derivados dos ajustes de sdram-100mhz.md
static double fill_cycles(const cfg_t *c, int line) {
    double tc = 1000.0 / c->f_mhz;                       // ns por ciclo do nucleo
    double total_ns = (178.0 + 20.2 * tc);               // falha de linha de 16 B (ajuste de 2 pontos)
    total_ns += 80.0 * (line / 16 - 1);                  // 8 beats de 16 bits por 16 B adicionais
    return c->fill_scale * total_ns / tc;
}
static double store_cycles(const cfg_t *c) {
    double tc = 1000.0 / c->f_mhz;
    return c->store_scale * (80.0 + 7.0 * tc) / tc;
}
static double evict_cycles(const cfg_t *c, int line) {
    double tc = 1000.0 / c->f_mhz;
    double lw = 60.0 + 80.0 * (line / 16 - 1);
    return 9.0 + (store_cycles(c) - 3.0) + lw / tc;
}

static void cfg_alloc(cfg_t *c) {
    cache_init(&c->ic, c->ic_kib, c->ic_line, c->ic_assoc);
    cache_init(&c->dc, c->dc_kib, c->dc_line, c->dc_assoc);
}

static uint8_t *scr_map;  // 1 bit por pagina de 4 KiB (16384 paginas) para a memoria interna: por configuracao
typedef struct { uint8_t *m; } scr_t;

#define NPAGE 16384
static uint64_t heat_train[NPAGE];
static int heat_rank_cache_n = -1;
static int rank_order[NPAGE];
static int cmp_heat(const void *a, const void *b) {
    uint64_t x = heat_train[*(const int *)a], y = heat_train[*(const int *)b];
    return x < y ? 1 : x > y ? -1 : 0;
}
static uint8_t scr_bits[NPAGE];  // por configuracao: ligado antes de cada arquivo

static void scr_select(cfg_t *c, uint8_t *bits) {
    memset(bits, 0, NPAGE);
    for (int i = 0; i < c->scr_pages && i < NPAGE; i++) bits[rank_order[i]] = 1;
}

// ---- buffer de escrita
static void q_purge(cfg_t *c) {
    int j = 0;
    for (int i = 0; i < c->q_n; i++)
        if (c->q_done[i] > c->t) c->q_done[j++] = c->q_done[i];
    c->q_n = j;
}

static void wait_until(cfg_t *c, double until, double *acc) {
    if (until > c->t) {
        if (c->counting) *acc += until - c->t;
        c->t = until;
    }
}

// transacao bloqueante na SDRAM de duracao lat: o nucleo espera
static void blocking(cfg_t *c, double lat, double *acc) {
    double start = c->t > c->bus_free ? c->t : c->bus_free;
    double end = start + lat;
    if (c->counting) *acc += end - c->t;
    c->t = end;
    c->bus_free = end;
}

static void post_store(cfg_t *c, uint32_t o) {
    q_purge(c);
    uint32_t line = o >> 4;
    uint32_t word = (o >> 2) & 3;
    (void)word;
    if (c->q_n > 0 && c->q_last_line == line && c->q_last_start > c->t) {
        // funde na ultima entrada ainda nao iniciada: vira escrita de linha
        if (c->q_last_words < 4) {
            c->q_last_words++;
            double srv = c->q_last_words >= 2 ? c->srv_line_ns * c->f_mhz / 1000.0 : c->srv_word_ns * c->f_mhz / 1000.0;
            double d = srv - c->q_last_srv;
            if (d != 0) {
                c->q_last_srv = srv;
                c->q_done[c->q_n - 1] += d;
                c->bus_free += d;
            }
        }
        if (c->counting) c->w_dstore += c->hw;
        c->t += c->hw;
        return;
    }
    if (c->q_n >= c->wbuf) {
        // cheio: espera a entrada mais antiga terminar
        wait_until(c, c->q_done[0], &c->w_wbuf_full);
        q_purge(c);
    }
    double srv = c->srv_word_ns * c->f_mhz / 1000.0;
    double start = c->t > c->bus_free ? c->t : c->bus_free;
    c->q_done[c->q_n++] = start + srv;
    c->bus_free = start + srv;
    c->q_last_line = line;
    c->q_last_words = 1;
    c->q_last_start = start;
    c->q_last_srv = srv;
    if (c->counting) { c->w_dstore += c->hw; c->st_mem++; }
    c->t += c->hw;
}

// ---- acessos
static void do_fetch(cfg_t *c, uint32_t o) {
    uint32_t set, tg;
    int w = cache_find(&c->ic, o, &set, &tg);
    if (w >= 0) { c->ic.stamp[w] = ++c->ic.clk; return; }
    size_t v = cache_victim(&c->ic, set);
    c->ic.tag[v] = tg;
    c->ic.stamp[v] = ++c->ic.clk;
    if (c->counting) c->ifill++;
    blocking(c, fill_cycles(c, c->ic_line) - 1.0, &c->w_ifetch);
}

static int dcacheable(const cfg_t *c, int reg) {
    switch (reg) {
        case R_FB: return c->fb_cached;
        case R_WAD: return c->wad_cached;
        case R_LAST: return 0;
        default: return 1;
    }
}

static void do_data(cfg_t *c, int kind, uint32_t o, const uint8_t *scr) {
    int is_st = kind >= K_ST1;
    int reg = region_of(o);
    if (c->counting) { if (is_st) { c->st++; c->reg_st[reg]++; } else { c->ld++; c->reg_ld[reg]++; } }
    if (scr[o >> 12]) {  // memoria interna: sem espera, nao passa pela SDRAM nem pela cache
        if (c->counting) c->scr_acc++;
        return;
    }
    if (!dcacheable(c, reg)) {
        if (c->counting) { if (is_st) c->unc_st++; else c->unc_ld++; }
        if (is_st) {
            if (c->wbuf > 0) post_store(c, o);
            else { if (c->counting) c->st_mem++; blocking(c, store_cycles(c), &c->w_dunc); }
        } else {
            blocking(c, fill_cycles(c, 16) - 5.0, &c->w_dunc);  // load sem cache: 4 + k
        }
        return;
    }
    uint32_t set, tg;
    int w = cache_find(&c->dc, o, &set, &tg);
    if (c->pol == P_WT) {
        if (!is_st) {
            if (w >= 0) {
                c->dc.stamp[w] = ++c->dc.clk;
                if (c->counting) { c->dhit_ld++; c->w_dload_hit += c->hw; }
                c->t += c->hw;
            } else {
                size_t v = cache_victim(&c->dc, set);
                c->dc.tag[v] = tg;
                c->dc.stamp[v] = ++c->dc.clk;
                if (c->counting) { c->dfill++; c->reg_dmiss[reg]++; }
                blocking(c, fill_cycles(c, c->dc_line), &c->w_dload_fill);
            }
        } else {
            if (w >= 0) c->dc.stamp[w] = ++c->dc.clk;  // acerto atualiza a linha e a memoria
            if (c->wbuf > 0) post_store(c, o);
            else { if (c->counting) c->st_mem++; blocking(c, store_cycles(c), &c->w_dstore); }
        }
    } else {  // write-back, write-allocate
        if (w >= 0) {
            c->dc.stamp[w] = ++c->dc.clk;
            if (is_st) c->dc.dirty[w] = 1;
            if (c->counting) { if (is_st) c->dhit_st++; else c->dhit_ld++; c->w_dload_hit += c->hw; }
            c->t += c->hw;
        } else {
            size_t v = cache_victim(&c->dc, set);
            if (c->dc.tag[v] != 0xFFFFFFFFu && c->dc.dirty[v]) {
                if (c->counting) c->dwb++;
                blocking(c, evict_cycles(c, c->dc_line), &c->w_evict);
            }
            c->dc.tag[v] = tg;
            c->dc.dirty[v] = is_st ? 1 : 0;
            c->dc.stamp[v] = ++c->dc.clk;
            if (c->counting) { c->dfill++; c->reg_dmiss[reg]++; }
            blocking(c, fill_cycles(c, c->dc_line), &c->w_dload_fill);
        }
    }
}

// Cenario CMAP256: no lugar de cmap_to_fb (laco de ~29 instrucoes por pixel que monta XRGB32),
// copia as 16000 palavras do I_VideoBuffer (indices de 8 bits) para a janela do video (periferico):
// por palavra lw, sw, 2 addi, bne = 5 instrucoes; o load passa pela dcache, o store custa vst ciclos
// (periferico, fora da SDRAM e da cache).
static void synth_cm(cfg_t *c, uint32_t base, const uint8_t *scr) {
    for (int i = 0; i < 80; i++) {  // uma chamada = uma linha de 320 pixels = 80 palavras
        if (c->counting) c->instr += 5;
        c->t += 5.0 * (1.0 + c->nm);
        do_data(c, K_LD4, (base & ~3u) + 4u * (uint32_t)i, scr);
        c->t += c->vst;
        if (c->counting) c->w_dunc += c->vst;
    }
}

// ---- configuracao
static void parse_cfg(cfg_t *c, const char *spec) {
    memset(c, 0, sizeof *c);
    c->f_mhz = 76.923077;
    c->ic_kib = 4; c->ic_line = 16; c->ic_assoc = 1;
    c->dc_kib = 4; c->dc_line = 16; c->dc_assoc = 1;
    c->pol = P_WT; c->fb_cached = 1; c->wad_cached = 1;
    c->srv_word_ns = 100; c->srv_line_ns = 170;
    c->fill_scale = 1; c->store_scale = 1;
    c->nm = 0.2266;
    c->hw = 2.0;
    c->vst = 4.0;
    char buf[512];
    strncpy(buf, spec, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *save;
    char *tok = strtok_r(buf, ":", &save);
    strncpy(c->name, tok, sizeof c->name - 1);
    while ((tok = strtok_r(NULL, ":", &save))) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = tok, *v = eq + 1;
        if (!strcmp(k, "f")) c->f_mhz = atof(v);
        else if (!strcmp(k, "ic")) sscanf(v, "%d,%d,%d", &c->ic_kib, &c->ic_line, &c->ic_assoc);
        else if (!strcmp(k, "dc")) sscanf(v, "%d,%d,%d", &c->dc_kib, &c->dc_line, &c->dc_assoc);
        else if (!strcmp(k, "pol")) c->pol = !strcmp(v, "wb") ? P_WB : P_WT;
        else if (!strcmp(k, "fb")) c->fb_cached = atoi(v);
        else if (!strcmp(k, "wad")) c->wad_cached = atoi(v);
        else if (!strcmp(k, "wbuf")) c->wbuf = atoi(v);
        else if (!strcmp(k, "scr")) c->scr_pages = atoi(v);
        else if (!strcmp(k, "fs")) c->fill_scale = atof(v);
        else if (!strcmp(k, "ss")) c->store_scale = atof(v);
        else if (!strcmp(k, "nm")) c->nm = atof(v);
        else if (!strcmp(k, "hw")) c->hw = atof(v);
        else if (!strcmp(k, "cm")) c->cm = atoi(v);
        else if (!strcmp(k, "vst")) c->vst = atof(v);
        else if (!strcmp(k, "sw")) c->srv_word_ns = atof(v);
        else if (!strcmp(k, "sl")) c->srv_line_ns = atof(v);
    }
}

// M10K: dados em blocos de 8192 bits uteis (32 b x 256), etiquetas em blocos de 10240 bits
static int m10k_cache(int kib, int line, int assoc, int dirty) {
    int lines = kib * 1024 / line;
    int sets = lines / assoc;
    int tagb = 26 - (__builtin_ctz((unsigned)line)) - __builtin_ctz((unsigned)sets) + 1 /*regiao*/;
    int meta = tagb + 1 + (dirty ? 1 : 0);
    long tagbits = (long)lines * meta;
    int blocks = kib + (int)((tagbits + 10239) / 10240);
    return blocks;
}

int main(int argc, char **argv) {
    cfg_t *cf = calloc(256, sizeof(cfg_t));
    int ncf = 0;
    const char *prof = NULL, *train = NULL, *outp = NULL;
    int warm = 2;
    int nfiles = 0;
    const char *files[64];
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) parse_cfg(&cf[ncf++], argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) prof = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) train = argv[++i];
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) warm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outp = argv[++i];
        else files[nfiles++] = argv[i];
    }
    static uint32_t buf[1 << 18];
    // ---- treino da memoria interna: calor por pagina nas leituras e escritas de dado
    if (train) {
        FILE *f = fopen(train, "rb");
        if (!f) { perror(train); return 1; }
        size_t n;
        int marks = 0;
        while ((n = fread(buf, 4, sizeof buf / 4, f)) > 0)
            for (size_t i = 0; i < n; i++) {
                uint32_t k = buf[i] >> 26, o = buf[i] & 0x3FFFFFF;
                if (k == K_MARK) marks++;
                else if (k >= K_LD1 && marks > warm) heat_train[o >> 12]++;
            }
        fclose(f);
        for (int i = 0; i < NPAGE; i++) rank_order[i] = i;
        qsort(rank_order, NPAGE, sizeof(int), cmp_heat);
    } else {
        for (int i = 0; i < NPAGE; i++) rank_order[i] = i;
    }
    for (int i = 0; i < ncf; i++) cfg_alloc(&cf[i]);
    static uint8_t scr[256][NPAGE];
    for (int i = 0; i < ncf; i++) scr_select(&cf[i], scr[i]);

    // ---- perfil (contagens independentes de configuracao)
    static uint64_t p_ld[NREG], p_st[NREG], p_ldb[NREG][3], p_stb[NREG][3];
    static uint64_t page_ld[NPAGE], page_st[NPAGE], page_if[NPAGE], pc_hist[0x10000];
    static uint8_t *lines_touched[NREG];
    static uint64_t p_lines[NREG], ifetch_lines;
    static uint8_t *if_lines;
    uint64_t p_instr = 0, p_frames = 0;
    if (prof) {
        for (int r = 0; r < NREG; r++) lines_touched[r] = calloc(1 << 22, 1);
        if_lines = calloc(1 << 22, 1);
    }

    for (int fi = 0; fi < nfiles; fi++) {
        FILE *f = fopen(files[fi], "rb");
        if (!f) { perror(files[fi]); return 1; }
        for (int i = 0; i < ncf; i++) {
            cache_reset(&cf[i].ic);
            cache_reset(&cf[i].dc);
            cf[i].t = 0; cf[i].bus_free = 0; cf[i].q_n = 0; cf[i].counting = 0;
        }
        int marks = 0;
        int counting = 0;
        size_t n;
        while ((n = fread(buf, 4, sizeof buf / 4, f)) > 0) {
            for (size_t r = 0; r < n; r++) {
                uint32_t k = buf[r] >> 26, o = buf[r] & 0x3FFFFFF;
                if (k == K_MARK) {
                    marks++;
                    if (marks == warm + 1) {
                        counting = 1;
                        for (int i = 0; i < ncf; i++) { cf[i].counting = 1; cf[i].t_start = cf[i].t; }
                    }
                    if (counting && marks > warm + 1) p_frames++;
                    continue;
                }
                if (k == K_FETCH) {
                    if (counting) p_instr++;
                    int in_range = (o >= CM_LO && o < CM_HI);
                    for (int i = 0; i < ncf; i++) {
                        if (cf[i].cm) {
                            if (in_range) { cf[i].in_cm = 1; continue; }
                            cf[i].in_cm = 0;
                        }
                        if (counting) cf[i].instr++;
                        cf[i].t += 1.0 + cf[i].nm;
                        do_fetch(&cf[i], o);
                    }
                    if (!in_range) for (int i = 0; i < ncf; i++) cf[i].cm_done = 0;
                    if (counting && prof) {
                        page_if[o >> 12]++;
                        if (o < 0x40000) pc_hist[o >> 2]++;
                        if_lines[o >> 4 >> 0 & 0x3FFFFF] |= 1;
                    }
                } else {
                    for (int i = 0; i < ncf; i++) {
                        if (cf[i].cm && cf[i].in_cm) {
                            // primeiro load de byte da conversao = *in (base do I_VideoBuffer): troca o laco por uma copia de palavras
                            if (k == K_LD1 && !cf[i].cm_done) { cf[i].cm_done = 1; synth_cm(&cf[i], o, scr[i]); }
                            continue;
                        }
                        do_data(&cf[i], (int)k, o, scr[i]);
                    }
                    if (counting && prof) {
                        int reg = region_of(o);
                        int sz = (k - 1) % 3;
                        if (k >= K_ST1) { p_st[reg]++; p_stb[reg][sz]++; page_st[o >> 12]++; }
                        else { p_ld[reg]++; p_ldb[reg][sz]++; page_ld[o >> 12]++; }
                        lines_touched[reg][(o >> 4) & 0x3FFFFF] = 1;
                    }
                }
            }
        }
        fclose(f);
    }
    // ---- saida
    FILE *out = outp ? fopen(outp, "w") : stdout;
    fprintf(out, "cfg\tf_mhz\tinstr\tquadros\tinstr_quadro\tifill_1000\tdfill_1000\tdwb_1000\tld_1000\tst_1000\tstmem_1000\t"
                 "w_dados_1000\tw_dload_hit_1000\tw_dload_fill_1000\tw_dstore_1000\tw_evict_1000\tw_wbfull_1000\tw_dunc_1000\t"
                 "w_ifetch_1000\tcpi\tfps\tm10k_ic\tm10k_dc\tm10k_scr\tm10k_total\n");
    for (int i = 0; i < ncf; i++) {
        cfg_t *c = &cf[i];
        double ins = (double)c->instr;
        double wd = c->w_dload_hit + c->w_dload_fill + c->w_dstore + c->w_evict + c->w_wbuf_full + c->w_dunc;
        double cyc = c->t - c->t_start;  // so do ultimo arquivo: usar soma abaixo
        (void)cyc;
        // ciclos totais = instr*(1+nm) + esperas
        double total = ins * (1.0 + c->nm) + wd + c->w_ifetch;
        double cpi = total / ins;
        double ipf = p_frames ? ins / (double)p_frames : 0;
        double fps = c->f_mhz * 1e6 / (cpi * ipf);
        int mic = m10k_cache(c->ic_kib, c->ic_line, c->ic_assoc, 0);
        int mdc = m10k_cache(c->dc_kib, c->dc_line, c->dc_assoc, c->pol == P_WB);
        int mscr = (c->scr_pages * 4 * 1024 * 8 + 8191) / 8192;
        fprintf(out, "%s\t%.3f\t%llu\t%llu\t%.0f\t%.2f\t%.2f\t%.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.4f\t%.3f\t%d\t%d\t%d\t%d\n",
                c->name, c->f_mhz, (unsigned long long)c->instr, (unsigned long long)p_frames, ipf,
                1000.0 * c->ifill / ins, 1000.0 * c->dfill / ins, 1000.0 * c->dwb / ins, 1000.0 * c->ld / ins,
                1000.0 * c->st / ins, 1000.0 * c->st_mem / ins, 1000.0 * wd / ins, 1000.0 * c->w_dload_hit / ins,
                1000.0 * c->w_dload_fill / ins, 1000.0 * c->w_dstore / ins, 1000.0 * c->w_evict / ins,
                1000.0 * c->w_wbuf_full / ins, 1000.0 * c->w_dunc / ins, 1000.0 * c->w_ifetch / ins, cpi, fps, mic,
                mdc, mscr, mic + mdc + mscr);
    }
    if (outp) fclose(out);
    if (prof) {
        FILE *pf = fopen(prof, "w");
        fprintf(pf, "# instr %llu quadros %llu\n", (unsigned long long)p_instr, (unsigned long long)p_frames);
        fprintf(pf, "regiao\tloads\tstores\tld_b\tld_h\tld_w\tst_b\tst_h\tst_w\tlinhas16_distintas\tKiB_tocados\n");
        for (int r = 0; r < NREG; r++) {
            uint64_t nl = 0;
            for (int j = 0; j < (1 << 22); j++) nl += lines_touched[r][j];
            fprintf(pf, "%s\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%.1f\n", REGN[r],
                    (unsigned long long)p_ld[r], (unsigned long long)p_st[r], (unsigned long long)p_ldb[r][0],
                    (unsigned long long)p_ldb[r][1], (unsigned long long)p_ldb[r][2], (unsigned long long)p_stb[r][0],
                    (unsigned long long)p_stb[r][1], (unsigned long long)p_stb[r][2], (unsigned long long)nl, nl * 16 / 1024.0);
        }
        uint64_t ni = 0;
        for (int j = 0; j < (1 << 22); j++) ni += if_lines[j];
        for (int p = 0; p < 0x10000; p++)
            if (pc_hist[p]) fprintf(pf, "H\t0x%08x\t%llu\n", 0x40000000u + (unsigned)p * 4u, (unsigned long long)pc_hist[p]);
        fprintf(pf, "# linhas de 16 B de instrucao distintas: %llu (%.1f KiB)\n", (unsigned long long)ni, ni * 16 / 1024.0);
        fprintf(pf, "# paginas de 4 KiB: pagina_base\tbusca\tload\tstore\n");
        for (int p = 0; p < NPAGE; p++)
            if (page_if[p] + page_ld[p] + page_st[p])
                fprintf(pf, "P\t0x%08x\t%llu\t%llu\t%llu\n", 0x40000000u + (unsigned)p * 4096u, (unsigned long long)page_if[p],
                        (unsigned long long)page_ld[p], (unsigned long long)page_st[p]);
        fclose(pf);
    }
    return 0;
}
