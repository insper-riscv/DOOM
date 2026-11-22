/* vp_test.c -- teste de video (VGA) e teclado (PS/2) da plataforma doom, para rodar da SDRAM.
 *
 * Video: programa a paleta de 256 entradas e desenha, no framebuffer indexado de 320x200 do VIDEO, oito barras de
 * cores, uma rampa de cinza, uma moldura de 1 pixel, um quadrado que se move devagar e quatro caixas de seta que
 * acendem enquanto a tecla correspondente esta pressionada.
 * Teclado: le o KBD por sondagem e imprime pela JTAG UART, para cada evento, a sequencia de bytes do conjunto 2
 * (make, F0 de break, E0 de estendido), o nome da tecla e se foi pressionada ou solta.
 *
 * Sem libc: so acessos a registradores e a UART. Enderecos da plataforma doom (platform.yaml).
 */
#include <stdbool.h>
#include <stdint.h>

#define VIDEO_BASE 0x80100000u
#define KBD_BASE 0x80007000u
#define UART_BASE 0xC0000000u
#define MTIME_LO 0xE000BFF8u

#define REG(addr) (*(volatile uint32_t *)(addr))

/* VIDEO: registradores em palavras (video.md, secao 4), paleta a partir da palavra 256, framebuffer em 0x20000. */
#define VID_CONTROL REG(VIDEO_BASE + 0u)
#define VID_FRAMES REG(VIDEO_BASE + 8u)
#define VID_GEOMETRY REG(VIDEO_BASE + 12u)
#define VID_FORMAT REG(VIDEO_BASE + 16u)
#define VID_PALETTE(i) REG(VIDEO_BASE + 0x400u + 4u * (uint32_t)(i))
#define FB ((volatile uint8_t *)(VIDEO_BASE + 0x20000u))

#define FB_W 320
#define FB_H 200

/* KBD (ps2_keyboard.md, secao 3). */
#define KBD_DATA REG(KBD_BASE + 0u)
#define KBD_STATUS REG(KBD_BASE + 4u)
#define KBD_RX_VALID 0x01u
#define KBD_RX_OVERFLOW 0x04u
#define KBD_RX_ERROR 0x08u

/* UART JTAG (stdio.c da plataforma): dado em +0, estado em +8 (bits 7:0 lugares livres, bit 17 hospedeiro ouvindo). */
#define UART_TX REG(UART_BASE + 0u)
#define UART_STATUS REG(UART_BASE + 8u)

/* Indices fixos da paleta. */
enum {
    C_BLACK = 0, C_WHITE, C_YELLOW, C_CYAN, C_GREEN, C_MAGENTA, C_RED, C_BLUE,
    C_ORANGE, C_DARKGRAY, C_BOXBG, C_UNUSED11, C_UNUSED12, C_BOXLIT, C_UNUSED14, C_UNUSED15,
    C_RAMP0 = 16
};

/* ---------------------------------------------------------------- UART */

static void uart_putc(char c)
{
    uint32_t st = UART_STATUS;
    if (((st >> 17) & 1u) == 0u)
        return; /* ninguem ouvindo: descarta, como a stdio do runtime */
    for (uint32_t polls = 0; (st & 0xFFu) == 0u; polls++) {
        if (polls == 200000u)
            return;
        st = UART_STATUS;
    }
    UART_TX = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0')
        uart_putc(*s++);
}

static void uart_hex8(uint8_t v)
{
    static const char digits[] = "0123456789ABCDEF";
    uart_putc(digits[v >> 4]);
    uart_putc(digits[v & 15u]);
}

static void uart_dec(uint32_t v)
{
    char buf[10];
    int n = 0;
    do {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u);
    while (n > 0)
        uart_putc(buf[--n]);
}

/* ---------------------------------------------------------------- video */

static void put_px(int x, int y, uint8_t idx)
{
    FB[y * FB_W + x] = idx;
}

static void fill_rect(int x0, int y0, int w, int h, uint8_t idx)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            put_px(x, y, idx);
}

/* Cor RGB444: vermelho nos bits 11:8, verde em 7:4, azul em 3:0. */
static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return (r << 8) | (g << 4) | b;
}

static void palette_init(void)
{
    VID_PALETTE(C_BLACK) = rgb(0, 0, 0);
    VID_PALETTE(C_WHITE) = rgb(15, 15, 15);
    VID_PALETTE(C_YELLOW) = rgb(15, 15, 0);
    VID_PALETTE(C_CYAN) = rgb(0, 15, 15);
    VID_PALETTE(C_GREEN) = rgb(0, 15, 0);
    VID_PALETTE(C_MAGENTA) = rgb(15, 0, 15);
    VID_PALETTE(C_RED) = rgb(15, 0, 0);
    VID_PALETTE(C_BLUE) = rgb(0, 0, 15);
    VID_PALETTE(C_ORANGE) = rgb(15, 8, 0);
    VID_PALETTE(C_DARKGRAY) = rgb(4, 4, 4);
    VID_PALETTE(C_BOXBG) = rgb(0, 0, 5);
    VID_PALETTE(C_BOXLIT) = rgb(0, 5, 0);
    VID_PALETTE(C_UNUSED11) = rgb(15, 15, 15);
    VID_PALETTE(C_UNUSED12) = rgb(15, 15, 15);
    VID_PALETTE(C_UNUSED14) = rgb(15, 15, 15);
    VID_PALETTE(C_UNUSED15) = rgb(15, 15, 15);
    /* As 240 entradas restantes formam a rampa: o nivel de cinza (0 a 15) cresce com o indice. */
    for (uint32_t k = 0; k < 240u; k++) {
        uint32_t level = k / 15u;
        VID_PALETTE(C_RAMP0 + k) = rgb(level, level, level);
    }
}

/* Layout em linhas do framebuffer. */
#define BARS_Y 1
#define BARS_H 79
#define RAMP_Y 80
#define RAMP_H 20
#define LANE_Y 100
#define LANE_H 32
#define SQ_SIZE 16
#define SQ_Y 108
#define BOX_Y 136
#define BOX_W 64
#define BOX_H 48
#define HB_Y 190

static void draw_static(void)
{
    static const uint8_t bars[8] = {C_WHITE, C_YELLOW, C_CYAN, C_GREEN, C_MAGENTA, C_RED, C_BLUE, C_BLACK};

    fill_rect(0, 0, FB_W, FB_H, C_BLACK);
    for (int i = 0; i < 8; i++)
        fill_rect(i * 40, BARS_Y, 40, BARS_H, bars[i]);
    /* Rampa: 320 colunas sobre as 240 entradas da rampa, 20 colunas por nivel visivel. */
    for (int x = 0; x < FB_W; x++)
        for (int y = RAMP_Y; y < RAMP_Y + RAMP_H; y++)
            put_px(x, y, (uint8_t)(C_RAMP0 + (x * 3) / 4));
    /* Moldura de 1 pixel na borda de 320x200. */
    for (int x = 0; x < FB_W; x++) {
        put_px(x, 0, C_WHITE);
        put_px(x, FB_H - 1, C_WHITE);
    }
    for (int y = 0; y < FB_H; y++) {
        put_px(0, y, C_WHITE);
        put_px(FB_W - 1, y, C_WHITE);
    }
}

/* Setas: 0 esquerda, 1 baixo, 2 cima, 3 direita (a ordem do teclado). */
enum { ARROW_LEFT = 0, ARROW_DOWN, ARROW_UP, ARROW_RIGHT };

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

/* Diz se o pixel (u, v) da caixa faz parte do desenho da seta. */
static bool arrow_pixel(int dir, int u, int v)
{
    int p;
    int c;
    int length;
    int center;

    if (dir == ARROW_UP || dir == ARROW_DOWN) {
        p = (dir == ARROW_UP) ? v : (BOX_H - 1 - v);
        c = u;
        length = BOX_H;
        center = BOX_W / 2;
    } else {
        p = (dir == ARROW_LEFT) ? u : (BOX_W - 1 - u);
        c = v;
        length = BOX_W;
        center = BOX_H / 2;
    }
    if (p >= 3 && p <= 27)
        return iabs(c - center) <= p - 3;
    if (p >= 28 && p < length - 4)
        return iabs(c - center) <= 8;
    return false;
}

static int box_x(int dir)
{
    return 20 + dir * 76;
}

static void draw_arrow_box(int dir, bool lit)
{
    uint8_t bg = lit ? C_BOXLIT : C_BOXBG;
    uint8_t fg = lit ? C_GREEN : C_DARKGRAY;
    int x0 = box_x(dir);

    for (int v = 0; v < BOX_H; v++)
        for (int u = 0; u < BOX_W; u++)
            put_px(x0 + u, BOX_Y + v, arrow_pixel(dir, u, v) ? fg : bg);
}

static void draw_square(int x, uint8_t idx)
{
    fill_rect(x, SQ_Y, SQ_SIZE, SQ_SIZE, idx);
}

/* ---------------------------------------------------------------- teclado */

struct key_name {
    uint8_t code;
    const char *name;
};

/* Conjunto 2 sem prefixo. */
static const struct key_name KEYS[] = {
    {0x01, "F9"}, {0x03, "F5"}, {0x04, "F3"}, {0x05, "F1"}, {0x06, "F2"}, {0x07, "F12"}, {0x09, "F10"},
    {0x0A, "F8"}, {0x0B, "F6"}, {0x0C, "F4"}, {0x0D, "TAB"}, {0x0E, "` (acento grave)"},
    {0x11, "ALT esquerdo"}, {0x12, "SHIFT esquerdo"}, {0x14, "CTRL esquerdo"},
    {0x15, "Q"}, {0x16, "1"}, {0x1A, "Z"}, {0x1B, "S"}, {0x1C, "A"}, {0x1D, "W"}, {0x1E, "2"},
    {0x21, "C"}, {0x22, "X"}, {0x23, "D"}, {0x24, "E"}, {0x25, "4"}, {0x26, "3"},
    {0x29, "ESPACO"}, {0x2A, "V"}, {0x2B, "F"}, {0x2C, "T"}, {0x2D, "R"}, {0x2E, "5"},
    {0x31, "N"}, {0x32, "B"}, {0x33, "H"}, {0x34, "G"}, {0x35, "Y"}, {0x36, "6"},
    {0x3A, "M"}, {0x3B, "J"}, {0x3C, "U"}, {0x3D, "7"}, {0x3E, "8"},
    {0x41, ", (virgula)"}, {0x42, "K"}, {0x43, "I"}, {0x44, "O"}, {0x45, "0"}, {0x46, "9"},
    {0x49, ". (ponto)"}, {0x4A, "/ (barra)"}, {0x4B, "L"}, {0x4C, "; (ponto e virgula)"}, {0x4D, "P"},
    {0x4E, "- (hifen)"}, {0x52, "' (apostrofo)"}, {0x54, "[ (colchete)"}, {0x55, "= (igual)"},
    {0x58, "CAPS LOCK"}, {0x59, "SHIFT direito"}, {0x5A, "ENTER"}, {0x5B, "] (colchete)"}, {0x5D, "\\ (contrabarra)"},
    {0x66, "BACKSPACE"}, {0x69, "KP 1"}, {0x6B, "KP 4"}, {0x6C, "KP 7"},
    {0x70, "KP 0"}, {0x71, "KP ."}, {0x72, "KP 2"}, {0x73, "KP 5"}, {0x74, "KP 6"}, {0x75, "KP 8"},
    {0x76, "ESC"}, {0x77, "NUM LOCK"}, {0x78, "F11"}, {0x79, "KP +"}, {0x7A, "KP 3"}, {0x7B, "KP -"},
    {0x7C, "KP *"}, {0x7D, "KP 9"}, {0x7E, "SCROLL LOCK"}, {0x83, "F7"},
    {0x00, nullptr}
};

/* Conjunto 2 com o prefixo E0. */
static const struct key_name KEYS_E0[] = {
    {0x11, "ALT direito (AltGr)"}, {0x12, "(SHIFT falso do PrtSc/Insert)"}, {0x14, "CTRL direito"},
    {0x1F, "WIN esquerdo"}, {0x27, "WIN direito"}, {0x2F, "MENU"}, {0x4A, "KP /"}, {0x5A, "KP ENTER"},
    {0x69, "END"}, {0x6B, "SETA ESQUERDA"}, {0x6C, "HOME"}, {0x70, "INSERT"}, {0x71, "DELETE"},
    {0x72, "SETA BAIXO"}, {0x74, "SETA DIREITA"}, {0x75, "SETA CIMA"}, {0x7A, "PAGE DOWN"},
    {0x7C, "PRINT SCREEN"}, {0x7D, "PAGE UP"},
    {0x00, nullptr}
};

static const char *lookup(const struct key_name *table, uint8_t code)
{
    for (; table->name != nullptr; table++)
        if (table->code == code)
            return table->name;
    return "(tecla desconhecida)";
}

/* Estado do decodificador de scancodes. */
struct kbd_state {
    bool ext;       /* prefixo E0 visto */
    bool brk;       /* prefixo F0 visto */
    int pause_left; /* bytes ainda esperados da sequencia E1 (Pause/Break) */
    uint8_t seq[8]; /* bytes do evento em curso, para imprimir */
    int seq_len;
    uint32_t events;
    uint32_t bytes;
    uint32_t overflows;
    uint32_t errors;
};

static struct kbd_state kbd;

static void arrow_update(const struct kbd_state *k, uint8_t code, bool pressed)
{
    if (!k->ext)
        return;
    if (code == 0x6B)
        draw_arrow_box(ARROW_LEFT, pressed);
    else if (code == 0x72)
        draw_arrow_box(ARROW_DOWN, pressed);
    else if (code == 0x75)
        draw_arrow_box(ARROW_UP, pressed);
    else if (code == 0x74)
        draw_arrow_box(ARROW_RIGHT, pressed);
}

static void report_event(const char *name, bool pressed)
{
    uart_puts("KEY ");
    for (int i = 0; i < kbd.seq_len; i++) {
        uart_hex8(kbd.seq[i]);
        uart_putc(' ');
    }
    uart_puts("| ");
    uart_puts(name);
    uart_puts(pressed ? " | PRESSIONADA" : " | SOLTA");
    uart_puts("\r\n");
}

static void kbd_byte(uint8_t b)
{
    kbd.bytes++;
    if (kbd.seq_len < 8)
        kbd.seq[kbd.seq_len++] = b;

    if (kbd.pause_left > 0) {
        kbd.pause_left--;
        if (kbd.pause_left == 0) {
            kbd.events++;
            report_event("PAUSE/BREAK", true);
            kbd.seq_len = 0;
        }
        return;
    }
    switch (b) {
    case 0xE0:
        kbd.ext = true;
        return;
    case 0xE1:
        kbd.pause_left = 7;
        return;
    case 0xF0:
        kbd.brk = true;
        return;
    case 0xFA:
    case 0xAA:
    case 0xFE:
    case 0x00:
    case 0xFF:
        if (!kbd.ext && !kbd.brk) {
            uart_puts("CTRL ");
            uart_hex8(b);
            uart_puts(b == 0xFA ? " | ACK\r\n" : b == 0xAA ? " | BAT ok (autoteste)\r\n" : " | resposta/erro do teclado\r\n");
            kbd.seq_len = 0;
            return;
        }
        break;
    default:
        break;
    }
    /* byte de tecla: completa o evento */
    bool pressed = !kbd.brk;
    const char *name = lookup(kbd.ext ? KEYS_E0 : KEYS, b);
    kbd.events++;
    report_event(name, pressed);
    arrow_update(&kbd, b, pressed);
    kbd.ext = false;
    kbd.brk = false;
    kbd.seq_len = 0;
}

static void kbd_poll(void)
{
    uint32_t st = KBD_STATUS;

    if ((st & KBD_RX_OVERFLOW) != 0u) {
        kbd.overflows++;
        KBD_STATUS = KBD_RX_OVERFLOW;
        uart_puts("AVISO: fila do teclado estourou (byte perdido)\r\n");
    }
    if ((st & KBD_RX_ERROR) != 0u) {
        kbd.errors++;
        KBD_STATUS = KBD_RX_ERROR;
        uart_puts("AVISO: erro de quadro ou paridade no PS/2\r\n");
    }
    while ((KBD_STATUS & KBD_RX_VALID) != 0u)
        kbd_byte((uint8_t)(KBD_DATA & 0xFFu));
}

/* ---------------------------------------------------------------- principal */

static void banner(void)
{
    uint32_t geometry = VID_GEOMETRY;
    uint32_t format = VID_FORMAT;

    uart_puts("\r\n[partida] === TESTE VIDEO+PS2 (doom) === VIDEO 0x80100000 KBD 0x80007000 | digite teclas; setas acendem na tela\r\n");
    uart_puts("VIDEO geometria=");
    uart_dec(geometry & 0xFFFFu);
    uart_putc('x');
    uart_dec(geometry >> 16);
    uart_puts(" bpp=");
    uart_dec(format & 0xFFu);
    uart_puts(" escala=");
    uart_dec((format >> 8) & 0xFFu);
    uart_puts(" bits/canal=");
    uart_dec((format >> 16) & 0xFFu);
    uart_puts((format >> 24) & 1u ? " indexado\r\n" : " direto\r\n");
}

int main(void)
{
    banner();
    palette_init();
    draw_static();
    for (int d = 0; d < 4; d++)
        draw_arrow_box(d, false);
    VID_CONTROL = 1u; /* liga a exibicao */
    uart_puts("[video ligado] paleta e desenho prontos\r\n");

    int sq_x = 2;
    int sq_dir = 1;
    draw_square(sq_x, C_ORANGE);
    uint32_t last_frame = VID_FRAMES;
    uint32_t frame_div = 0;
    uint32_t t_last = REG(MTIME_LO);
    uint32_t seconds = 0;
    bool hb = false;

    for (;;) {
        kbd_poll();

        uint32_t frame = VID_FRAMES;
        if (frame != last_frame) {
            last_frame = frame;
            frame_div++;
            if ((frame_div & 1u) == 0u) { /* um passo a cada 2 quadros: 30 pixels por segundo */
                draw_square(sq_x, C_BLACK);
                sq_x += sq_dir;
                if (sq_x + SQ_SIZE >= FB_W - 2 || sq_x <= 2)
                    sq_dir = -sq_dir;
                draw_square(sq_x, C_ORANGE);
            }
        }

        uint32_t now = REG(MTIME_LO);
        if (now - t_last >= 1000000u) { /* 1 MHz: um segundo */
            t_last += 1000000u;
            seconds++;
            hb = !hb;
            fill_rect(150, HB_Y, 20, 6, hb ? C_GREEN : C_BLACK);
            if ((seconds & 1u) == 0u) {
                uart_puts("HB t=");
                uart_dec(seconds);
                uart_puts("s quadros=");
                uart_dec(frame);
                uart_puts(" eventos=");
                uart_dec(kbd.events);
                uart_puts(" bytes=");
                uart_dec(kbd.bytes);
                uart_puts(" overflow=");
                uart_dec(kbd.overflows);
                uart_puts(" erros=");
                uart_dec(kbd.errors);
                uart_puts("\r\n");
            }
            if ((seconds % 10u) == 0u)
                banner();
        }
    }
}
