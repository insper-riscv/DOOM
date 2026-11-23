#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Monta o conjunto de WADs do menu (MENU=1 no build.sh) a partir de uma pasta com as suas WADs.
#
# Uso: scripts/wads.sh PASTA_DE_SAIDA
# Variaveis:
#   WAD_DIR  pasta onde estao as WADs (procura ate 4 niveis abaixo, sem diferenciar maiusculas de minusculas); se nao
#            vier, usa a que o `fetch.sh --wad-dir` guardou em .wad-dir
#   ONDEMAND=1  so empacota (wads.bin alinhado a 4 KiB) para o cartao microSD (SD=demand): a tabela guarda o deslocamento de
#            cada WAD no wads.bin no lugar de um endereco da SDRAM, nao confere o limite da SDRAM e escreve
#            wads.index.tsv (entrada do scripts/sdwrite.py). O conjunto inteiro (doom doom2 plutonia tnt master) cabe
#   WAD_SET  jogos do menu, em ordem (padrao "doom doom2 plutonia master"): doom, doom2, plutonia, tnt e master. O
#            master (Master Levels) usa o doom2.wad como base e exige o doom2 no conjunto
#
# Escreve em PASTA_DE_SAIDA:
#   wads.bin     as WADs em sequencia, cada uma alinhada a 4 KiB; e a unica imagem que o load-ram leva a SDRAM
#   wad_table.h  a tabela que o jogo e o menu leem (nome, endereco e tamanho de cada WAD, e as entradas do menu)
#   wads.env     o mapa da SDRAM que o build usa (WAD_ADDR, WAD_SIZE, FB_ADDR, HEAP_START, MEASURE_ADDR)
#   wads.txt     o resumo legivel, com o SHA-256 de cada arquivo
#
# O mapa que este script calcula cabe numa SDRAM de 64 MB: a imagem do jogo em 0x40000000 (ate 1 MiB), as WADs em
# 0x40100000, depois o framebuffer, o heap de 7 MB e o bloco de medicao, que terminam antes dos 64 KiB de backup do
# .data em 0x43800000 e da pilha, perto do topo.
set -eu
cd "$(dirname "$0")/.."

OUT=${1:?uso: scripts/wads.sh PASTA_DE_SAIDA}
WAD_SET=${WAD_SET:-doom doom2 plutonia master}
if [ -z "${WAD_DIR:-}" ] && [ -f .wad-dir ]; then
  WAD_DIR=$(cat .wad-dir)
fi
if [ -z "${WAD_DIR:-}" ] || [ ! -d "$WAD_DIR" ]; then
  echo "WAD_DIR ausente ou nao e uma pasta: rode ./fetch.sh --wad-dir PASTA (ou exporte WAD_DIR)" >&2
  exit 1
fi

WAD_BASE=0x40100000
# ONDEMAND: sem WAD na SDRAM, o orcamento da imagem do jogo (codigo, dados e bss, com o buffer do cartao) sobe para 2 MiB
if [ "${ONDEMAND:-0}" = 1 ]; then
  WAD_BASE=0x40200000
fi
ALIGN=4096
# Fim da area das WADs mais framebuffer, heap e bloco de medicao: tem de acabar antes disto
LIMIT=0x43780000
FB_RESERVE=$((0x40000))
HEAP_RESERVE=$((7 * 1024 * 1024))
MEASURE_ADDR=0x43780000

mkdir -p "$OUT"

# Acha um arquivo pelo nome, sem diferenciar maiusculas de minusculas
find_wad() {
  find -L "$WAD_DIR" -maxdepth 4 -type f -iname "$1" | sort | head -n 1
}

# Titulo e descricao de cada jogo, na ordem do menu
title_of() {
  case "$1" in
    doom) echo "ULTIMATE DOOM" ;;
    doom2) echo "DOOM II" ;;
    plutonia) echo "PLUTONIA" ;;
    tnt) echo "TNT: EVILUTION" ;;
  esac
}
subtitle_of() {
  case "$1" in
    doom) echo "4 EPISODES - 36 LEVELS" ;;
    doom2) echo "HELL ON EARTH - 32 LEVELS" ;;
    plutonia) echo "THE PLUTONIA EXPERIMENT - 32 LEVELS" ;;
    tnt) echo "FINAL DOOM - 32 LEVELS" ;;
  esac
}

LIST="$OUT/wads.list"
: > "$LIST"
HAS_MASTER=0
HAS_DOOM2=0
for game in $WAD_SET; do
  case "$game" in
    doom | doom2 | plutonia | tnt)
      f=$(find_wad "$game.wad")
      if [ -z "$f" ]; then
        echo "$game.wad nao esta em $WAD_DIR" >&2
        exit 1
      fi
      printf 'iwad\t%s\t%s\n' "$game" "$f" >> "$LIST"
      if [ "$game" = doom2 ]; then
        HAS_DOOM2=1
      fi
      ;;
    master)
      HAS_MASTER=1
      ;;
    *)
      echo "WAD_SET: jogo desconhecido '$game' (use doom, doom2, plutonia, tnt, master)" >&2
      exit 1
      ;;
  esac
done

MASTER_MAP=""
if [ "$HAS_MASTER" = 1 ]; then
  if [ "$HAS_DOOM2" = 0 ]; then
    echo "master (Master Levels) usa o doom2.wad como base: ponha doom2 no WAD_SET" >&2
    exit 1
  fi
  MASTER_MAP=$(find -L "$WAD_DIR" -maxdepth 4 -type f -iname 'doom2.map' | sort | head -n 1)
  if [ -z "$MASTER_MAP" ]; then
    echo "master: nao achei o DOOM2.MAP do Master Levels em $WAD_DIR" >&2
    exit 1
  fi
  MASTER_DIR=$(dirname "$MASTER_MAP")
  # Cada linha "nome.wad: MAPnn" diz em que fase o Master Levels entra; a linha seguinte so com "MAPnn" (teeth) e extra
  grep -i '\.wad:' "$MASTER_MAP" | tr -d '\r' | while IFS= read -r line; do
    name=$(printf '%s' "$line" | sed 's/:.*//' | tr 'A-Z' 'a-z')
    map=$(printf '%s' "$line" | sed 's/.*MAP\([0-9][0-9]*\).*/\1/I' | sed 's/^0*\([0-9]\)/\1/')
    f=$(find -L "$MASTER_DIR" -maxdepth 3 -type f -iname "$name" | sort | head -n 1)
    if [ -z "$f" ]; then
      echo "master: $name do DOOM2.MAP nao esta em $MASTER_DIR" >&2
      exit 1
    fi
    printf 'pwad\t%s\t%s\t%s\n' "$name" "$f" "$map" >> "$LIST"
  done
  if [ "$(grep -c '^pwad' "$LIST")" -eq 0 ]; then
    echo "master: o DOOM2.MAP de $MASTER_MAP nao listou nenhuma fase" >&2
    exit 1
  fi
fi

# Posiciona cada arquivo, em sequencia, alinhado a 4 KiB
TXT="$OUT/wads.txt"
: > "$TXT"
BLOB="$OUT/wads.bin"
: > "$BLOB"
: > "$OUT/wads.index.tsv"
HDR="$OUT/wad_table.h.body"
: > "$HDR"
offset=0
count=0
while IFS="$(printf '\t')" read -r kind name path map; do
  size=$(wc -c < "$path")
  case "$kind" in
    iwad) fname="$name.wad" ;;
    pwad) fname="$name" ;;
  esac
  pad=$(( (ALIGN - offset % ALIGN) % ALIGN ))
  if [ "$pad" -ne 0 ]; then
    head -c "$pad" /dev/zero >> "$BLOB"
    offset=$((offset + pad))
  fi
  if [ "${ONDEMAND:-0}" = 1 ]; then
    addr=$offset
  else
    addr=$((WAD_BASE + offset))
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$kind" "$fname" "$offset" "$size" "$map" >> "$OUT/wads.index.tsv"
  cat "$path" >> "$BLOB"
  sum=$(sha256sum "$path" | cut -d' ' -f1)
  printf '%-14s %10d bytes  0x%08X  sha256 %s\n' "$fname" "$size" "$addr" "$sum" >> "$TXT"
  printf '    { "%s", 0x%08XU, %uU },\n' "$fname" "$addr" "$size" >> "$HDR"
  offset=$((offset + size))
  count=$((count + 1))
done < "$LIST"

# Indices na tabela
index_of() {
  # $1 = nome do arquivo (minusculo); imprime o indice ou -1
  i=0
  while IFS="$(printf '\t')" read -r kind name path map; do
    case "$kind" in
      iwad) fname="$name.wad" ;;
      pwad) fname="$name" ;;
    esac
    if [ "$fname" = "$1" ]; then
      echo "$i"
      return
    fi
    i=$((i + 1))
  done < "$LIST"
  echo -1
}

WAD_SIZE=$offset
WAD_END=$(( (WAD_BASE + WAD_SIZE + ALIGN - 1) / ALIGN * ALIGN ))
if [ "${ONDEMAND:-0}" = 1 ]; then
  # Sem WAD na SDRAM: o framebuffer vem logo depois da imagem do jogo (que cabe em 2 MiB)
  WAD_END=$WAD_BASE
fi
FB_ADDR=$WAD_END
HEAP_START=$((FB_ADDR + FB_RESERVE))
HEAP_END=$((HEAP_START + HEAP_RESERVE))
if [ "${ONDEMAND:-0}" != 1 ] && [ "$HEAP_END" -gt "$((LIMIT))" ]; then
  echo "as WADs nao cabem na SDRAM com o heap de 7 MB: terminam em $(printf '0x%08X' "$WAD_END") e o heap iria ate" \
    "$(printf '0x%08X' "$HEAP_END"), acima de $(printf '0x%08X' "$LIMIT")." >&2
  echo "tire um jogo do WAD_SET (por exemplo troque plutonia por tnt, ou tire o master)" >&2
  cat "$TXT" >&2
  exit 1
fi

{
  echo "/* Gerado por scripts/wads.sh: nao edite. */"
  echo "#ifndef DOOM_WAD_TABLE_H"
  echo "#define DOOM_WAD_TABLE_H"
  echo "#include \"doom_menu.h\""
  echo "#define DOOM_WAD_COUNT $count"
  echo "static const struct doom_wad_entry doom_wad_table[DOOM_WAD_COUNT] ="
  echo "{"
  cat "$HDR"
  echo "};"
  # Jogos do menu: titulo, descricao, indice da IWAD
  gcount=0
  echo "static const struct doom_menu_game doom_menu_games[] ="
  echo "{"
  for game in $WAD_SET; do
    case "$game" in
      doom | doom2 | plutonia | tnt)
        printf '    { "%s", "%s", %d },\n' "$(title_of "$game")" "$(subtitle_of "$game")" "$(index_of "$game.wad")"
        gcount=$((gcount + 1))
        ;;
    esac
  done
  if [ "$HAS_MASTER" = 1 ]; then
    printf '    { "MASTER LEVELS", "20 EXTRA LEVELS FOR DOOM II", -1 },\n'
    gcount=$((gcount + 1))
  fi
  echo "};"
  echo "#define DOOM_MENU_GAME_COUNT $gcount"
  echo "#define DOOM_MENU_MASTER_INDEX $( [ "$HAS_MASTER" = 1 ] && echo $((gcount - 1)) || echo -1 )"
  echo "#define DOOM_MENU_MASTER_IWAD $( [ "$HAS_MASTER" = 1 ] && index_of doom2.wad || echo -1 )"
  # Fases do Master Levels: titulo (o nome do arquivo), indice da WAD e mapa
  mcount=0
  echo "static const struct doom_menu_master doom_menu_masters[] ="
  echo "{"
  while IFS="$(printf '\t')" read -r kind name path map; do
    if [ "$kind" = pwad ]; then
      title=$(printf '%s' "$name" | sed 's/\.wad$//' | tr 'a-z' 'A-Z')
      printf '    { "%s", %d, %d },\n' "$title" "$(index_of "$name")" "$map"
      mcount=$((mcount + 1))
    fi
  done < "$LIST"
  [ "$mcount" -eq 0 ] && echo "    { \"\", -1, 0 },"
  echo "};"
  echo "#define DOOM_MENU_MASTER_COUNT $mcount"
  echo "#endif"
} > "$OUT/wad_table.h"
rm -f "$HDR"

cat > "$OUT/wads.env" <<EOF
# Gerado por scripts/wads.sh: nao edite.
WAD_ADDR=$(printf '0x%08X' "$WAD_BASE")
WAD_SIZE=$( [ "${ONDEMAND:-0}" = 1 ] && echo 0 || echo "$WAD_SIZE" )
SD_ONDEMAND=${ONDEMAND:-0}
WADS_BIN_SIZE=$WAD_SIZE
FB_ADDR=$(printf '0x%08X' "$FB_ADDR")
FB_BYTES=256000
HEAP_START=$(printf '0x%08X' "$HEAP_START")
MEASURE_ADDR=$MEASURE_ADDR
EOF

echo "WADs do menu ($count arquivos, $WAD_SIZE bytes) em $WAD_DIR:"
cat "$TXT"
echo "framebuffer em $(printf '0x%08X' "$FB_ADDR"), heap desde $(printf '0x%08X' "$HEAP_START") (ate $(printf '0x%08X' "$HEAP_END"))"
