/*
 * Configuracao do FatFs R0.15a (ChaN, http://elm-chan.org/fsw/ff/) para o DOOM do Insper-RISC-V (SDFS=fatfs).
 * Mesmos nomes e revisao do ffconf.h da distribuicao; so os valores mudam. O build usa este arquivo no lugar do
 * original (o fetch.sh nao copia o ffconf.h do FatFs). O FatFs tem licenca permissiva (ver README, secao do FatFs).
 *
 * Somente leitura, FAT32, nomes 8.3, uma unidade, sem relogio, com o mapa de clusters (CLMT) para f_lseek rapido.
 */

#ifndef FFCONF_DEF
#define FFCONF_DEF 5380

#define FF_FS_READONLY 1  /* sem f_write, f_mkdir etc.: menos codigo e o cartao nunca e alterado */
#define FF_FS_MINIMIZE 0  /* f_stat, f_readdir e f_lseek ficam */
#define FF_USE_FIND 0
#define FF_USE_MKFS 0
#define FF_USE_FASTSEEK 1 /* CLMT: f_lseek em arquivo de 18 MB sem percorrer a FAT */
#define FF_USE_EXPAND 0
#define FF_USE_CHMOD 0
#define FF_USE_LABEL 0
#define FF_USE_FORWARD 0
#define FF_USE_STRFUNC 0
#define FF_PRINT_LLI 0
#define FF_PRINT_FLOAT 0
#define FF_STRF_ENCODE 3

#define FF_CODE_PAGE 437
#define FF_USE_LFN 0 /* nomes 8.3: DOOM.WAD, DOOM2.WAD, PLUTONIA.WAD, TNT.WAD e as fases do Master Levels */
#define FF_MAX_LFN 255
#define FF_LFN_UNICODE 0
#define FF_LFN_BUF 255
#define FF_SFN_BUF 12
#define FF_FS_RPATH 0 /* so caminhos absolutos */

#define FF_VOLUMES 1
#define FF_STR_VOLUME_ID 0
#define FF_VOLUME_STRS "SD"
#define FF_MULTI_PARTITION 0 /* uma unidade logica: a primeira particao do MBR (ou o volume sem MBR) */
#define FF_MIN_SS 512
#define FF_MAX_SS 512
#define FF_LBA64 0
#define FF_MIN_GPT 0x10000000
#define FF_USE_TRIM 0

#define FF_FS_TINY 0 /* cada FIL leva o seu buffer de setor (leituras pequenas de lumps nao disputam a janela da FAT) */
#define FF_FS_EXFAT 0
#define FF_FS_NORTC 1
#define FF_NORTC_MON 1
#define FF_NORTC_MDAY 1
#define FF_NORTC_YEAR 2025
#define FF_FS_NOFSINFO 0
#define FF_FS_LOCK 0
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT 1000

#endif
