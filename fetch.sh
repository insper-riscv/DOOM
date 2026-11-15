#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Baixa a doomgeneric (commit fixado) em doomgeneric/ e a doom1.wad shareware
# v1.9 em doom1.wad, e confere o SHA-256 e o SHA-1 da WAD.
# Precisa de git, curl e sha256sum no host.
set -eu
cd "$(dirname "$0")"

# Commit da doomgeneric (GPL-2.0) usado na porta
DG_COMMIT=e257a09527062bfb986b6e5e0780839b6cbd5b71
# SHA-256 e SHA-1 da doom1.wad shareware v1.9, 4196020 bytes
WAD_SHA256=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771
WAD_SHA1=5b2e249b9c5133ec987b3ea77596381dc0d6bc1d
WAD_URL="https://raw.githubusercontent.com/Doom-Utils/shareware-collection/master/Doom%201.9/doom1.wad"

if [ ! -d doomgeneric/.git ]; then
  git clone -q https://github.com/ozkl/doomgeneric.git doomgeneric
fi
git -C doomgeneric checkout -q "$DG_COMMIT"

if [ ! -f doom1.wad ]; then
  curl -fsSL -o doom1.wad "$WAD_URL"
fi
echo "$WAD_SHA256  doom1.wad" | sha256sum -c -
echo "$WAD_SHA1  doom1.wad" | sha1sum -c -
