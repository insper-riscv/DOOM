# Cartão microSD FAT32 para o `sdcard_FatFs`

Este guia prepara um cartão comum, formatado em FAT32, com as WADs como arquivos. É a versão `SDFS=fatfs` do DOOM na DE0-CV. O cartão cru (`sdcard_direct_mapped`) usa outro formato e outra imagem do jogo, e este guia não o cobre.

## 1. O que precisa estar no cartão

Tudo na raiz do cartão, com estes nomes (maiúsculas ou minúsculas, tanto faz, e sempre no formato 8.3):

| No cartão | Arquivo de origem (pasta do DOOM base) |
| :--- | :--- |
| `DOOM.WAD` | `DOOM.WAD` |
| `DOOM2.WAD` | `doom2/DOOM2.WAD` |
| `PLUTONIA.WAD` | `plutonia/PLUTONIA.WAD` |
| `TNT.WAD` | `tnt/TNT.WAD` |
| `MASTER/` (pasta) | as 20 fases de `master/wads/` e o arquivo `master/DOOM2.MAP` |

O Master Levels só aparece no menu junto do `DOOM2.WAD`. O menu mostra só o que achar no cartão.

## 2. Formatar em FAT32

Formatar apaga o cartão inteiro. Confirme que o dispositivo escolhido é o cartão e não um disco do computador.

| Sistema | Como |
| :--- | :--- |
| Windows | Botão direito no cartão, Formatar, sistema de arquivos FAT32, tamanho da unidade de alocação 32 KB. Para cartões acima de 32 GB o Windows não oferece FAT32, e então use o programa `guiformat` ou o `diskpart` com `format fs=fat32 unit=32768 quick` |
| macOS | Utilitário de Disco, Apagar, formato MS-DOS (FAT), esquema Master Boot Record |
| Linux | `lsblk` para achar o dispositivo, depois `sudo mkfs.vfat -F 32 -s 64 -n DOOM /dev/sdX1` (o `-s 64` é cluster de 32 KB) |

Cluster de 32 KB ou maior deixa a tabela FAT curta, o que ajuda a velocidade de leitura. Cartão com ou sem tabela de partições MBR funciona.

## 3. Copiar os arquivos

1. Copie `DOOM.WAD`, `DOOM2.WAD`, `PLUTONIA.WAD` e `TNT.WAD` para a raiz do cartão.
2. Crie a pasta `MASTER` na raiz.
3. Copie para dentro dela os 20 arquivos de `master/wads/` e o `DOOM2.MAP`.
4. Ejete o cartão pelo sistema (não arranque), para o sistema gravar tudo.

## 4. Conferir antes de levar à placa

| Verificação | Resultado esperado |
| :--- | :--- |
| Tamanho de `DOOM.WAD` | 12.408.292 bytes |
| Tamanho de `DOOM2.WAD` | 14.604.584 bytes |
| Tamanho de `PLUTONIA.WAD` | 17.420.824 bytes |
| Tamanho de `TNT.WAD` | 18.195.736 bytes |
| Arquivos em `MASTER` | 20 fases `.WAD` mais o `DOOM2.MAP` |
| Sistema de arquivos | FAT32 |

## 5. Na placa

O cartão vai no slot da DE0-CV, com a placa desligada ou com cuidado. A imagem do jogo para FatFs é gravada pelo computador que está ligado à placa por JTAG (não por este cartão). Se o menu não mostrar um jogo, a causa mais provável é um nome de arquivo fora do formato 8.3 ou uma pasta `MASTER` no lugar errado.
