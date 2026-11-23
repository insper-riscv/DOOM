# Compilação e o arquivo WAD

## 1. Tempo de compilação

Medido na workstation de desenvolvimento, com o GCC do contêiner `dev_tools`, sem cache de objetos (a pasta de saída é apagada antes de cada medida) e com a imagem do contêiner já baixada.

| Item | Valor |
| :--- | :--- |
| Processador | Intel Core i7-14700 (20 núcleos, 28 threads) |
| Memória | 31 GB |
| Compilador | `riscv32-unknown-elf-gcc` 16.1.0 (`ghcr.io/insper-riscv/dev_tools:latest`), alvo `rv32im`, `ilp32`, picolibc |
| Comando | `TARGET=board MENU=1 VIDEO=1 INTERACTIVE=1 SD=demand ONDEMAND=1 sh build.sh` (jogo com menu e cartão SD, mais a empacotação das WADs do usuário) |

| Otimização | Tempo | Tamanho de `doom.bin` |
| :--- | ---: | ---: |
| `-O3 -funroll-loops -flto` (padrão do `image.sh`) | 16,0 s | 592.636 bytes |
| `-O2` | 11,2 s | 425.540 bytes |

O tempo inclui a partida do contêiner, a compilação da `doomgeneric`, da porta e do runtime da plataforma, a ligação e a geração do binário. O `-flto` compila em modo serial (o GCC avisa `using serial compilation of 6 LTRANS jobs`), o que contribui para ele levar uns 5 s a mais que o `-O2`. A primeira execução num computador novo soma o `fetch.sh`, que baixa a `doomgeneric` num commit fixo e o runtime da plataforma, e a baixada da imagem do contêiner.

## 2. O que é um WAD

WAD vem de "Where's All the Data". É o arquivo único em que o DOOM guarda tudo o que não é código: mapas, texturas, sprites, sons, músicas, paletas e demos. O motor lê o WAD por nome, e quase tudo o que o jogo mostra ou toca vem dele.

### 2.1. Formato

| Parte | Tamanho | Conteúdo |
| :--- | ---: | :--- |
| Cabeçalho | 12 bytes | Identificação (`IWAD` ou `PWAD`), número de lumps (inteiro de 32 bits, little-endian) e deslocamento da tabela de lumps |
| Lumps | variável | Os dados, um após o outro |
| Tabela de lumps (diretório) | 16 bytes por lump | Deslocamento do lump no arquivo, tamanho e nome de até 8 caracteres |

Um **lump** é um bloco de dados com nome. Os principais:

| Lump | Conteúdo |
| :--- | :--- |
| `PLAYPAL` | 14 paletas de 256 cores (3 bytes por cor), a normal e as de dano, de item e de radiação |
| `COLORMAP` | 34 tabelas de 256 bytes que escurecem as cores por distância e luz |
| `TEXTURE1`, `TEXTURE2`, `PNAMES` | Montagem das texturas de parede a partir de remendos |
| Remendos e sprites | Imagens em colunas, usadas por paredes, monstros, HUD e menu |
| Pisos e tetos (flats) | Blocos de 64x64 pixels, sem cabeçalho |
| Mapa (`E1M1`, `MAP01` e outros) | `THINGS`, `LINEDEFS`, `SIDEDEFS`, `VERTEXES`, `SEGS`, `SSECTORS`, `NODES`, `SECTORS`, `REJECT` e `BLOCKMAP` |
| `DS*` e `D_*` | Efeitos sonoros e músicas |
| `DEMO1` a `DEMO4` | Gravações de teclas que o jogo reproduz sozinho |

### 2.2. IWAD e PWAD

| Tipo | Papel |
| :--- | :--- |
| **IWAD** (jogo base) | Contém o jogo inteiro: `DOOM.WAD`, `DOOM2.WAD`, `PLUTONIA.WAD` e `TNT.WAD` |
| **PWAD** (remendo) | Acrescenta ou troca lumps do IWAD, como as fases do Master Levels |

Quando há vários arquivos, o motor monta uma lista só e, para um nome repetido, vale o lump do arquivo carregado por último. É assim que uma fase do Master Levels substitui o mapa do DOOM II: `doom -iwad doom2.wad -file attack.wad -warp 1`.

### 2.3. Os arquivos deste projeto

As WADs comerciais não vêm no repositório: o usuário aponta a pasta onde as tem (`fetch.sh --wad-dir`).

| Arquivo | Tamanho (bytes) | Conteúdo |
| :--- | ---: | :--- |
| `DOOM.WAD` | 12.408.292 | The Ultimate DOOM, 4 episódios |
| `DOOM2.WAD` | 14.604.584 | DOOM II, 32 fases |
| `PLUTONIA.WAD` | 17.420.824 | The Plutonia Experiment |
| `TNT.WAD` | 18.195.736 | TNT: Evilution |
| Master Levels (20 PWADs) | 3.753.798 | Fases extras do DOOM II, mais o `DOOM2.MAP` que diz em qual mapa cada uma entra |
| `doom1.wad` (shareware) | 4.196.020 | Episódio 1, usada nas medições com o `demo1` |

As quatro IWADs mais o Master Levels somam 66.428.128 bytes quando empacotadas com alinhamento de 4 KiB, o que não cabe nos 64 MB da SDRAM junto com o jogo. Por isso existem os modos em que o jogo lê do cartão sob demanda.

## 3. Como a porta usa o WAD

| Modo | Onde fica a WAD | Como o motor lê |
| :--- | :--- | :--- |
| Memória (`load-ram`) | Na SDRAM, em `0x40100000`, gravada pelo JTAG (o `wads.sh` junta as escolhidas em `wads.bin` e gera a tabela de endereços) | `memcpy` de uma região da SDRAM |
| `sdcard_direct_mapped` (`SDFS=raw`) | No cartão SD, cru, a partir do setor 2056, com o cabeçalho no setor 2048 (`sdwrite.py`) | Leitura de blocos de 512 bytes por lump, no `W_ReadLump` |
| `sdcard_FatFs` (`SDFS=fatfs`) | Arquivos num cartão FAT32 | `f_lseek` e `f_read` do FatFs |

Nos modos de cartão, o `W_ReadLump` busca só o lump pedido, e a zona de memória do DOOM (o heap de 7 MB) guarda o que já foi lido, como o jogo original fazia com o disco. Um nível lê alguns MB de texturas e sprites logo que começa, e a tela de carga mostra o contador de MB lidos do cartão.
