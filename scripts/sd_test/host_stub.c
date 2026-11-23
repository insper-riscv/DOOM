/* Troca do doom_sd.c por leitura direta de uma imagem de arquivo: roda o FatFs e o doom_fatfs.c no host, sem simulador
 * (verificacao rapida das imagens e do leitor; o cocotb prova o mesmo caminho pelo VHDL). */
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "doom_menu.h"
#include "doom_sd.h"
#include "doom_wad_src.h"

struct doom_dyn_tables doom_dyn;
struct doom_wad_entry doom_dyn_wad_table[DOOM_SD_MAX_WADS];
struct doom_menu_game doom_dyn_games[DOOM_SD_MAX_WADS + 1];
struct doom_menu_master doom_dyn_masters[DOOM_SD_MAX_WADS];
static int fd = -1, on_card;
uint32_t blocks;
int host_open(const char *path) { fd = open(path, O_RDONLY); return fd; }
int doom_sd_is_ready(void) { return fd >= 0; }
int doom_sd_init(void) { return 0; }
uint32_t doom_sd_sector_count(void) { return 1; }
int doom_sd_read_block(uint32_t sector, void *dst)
{
    blocks++;
    return pread(fd, dst, 512, (off_t)sector * 512) == 512 ? 0 : -3;
}
uint32_t doom_sd_now_us(void) { return 0; }
void doom_sd_account(uint32_t e) { (void)e; }
void doom_sd_set_on_card(int v) { on_card = v; }
int doom_sd_wads_on_card(void) { return on_card; }
