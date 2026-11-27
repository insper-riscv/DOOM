# Instalar o DOOM na DE0-CV

🌐 [Português](INSTALAR.md) · [English](../en/INSTALL.md)

Este guia leva a DE0-CV, com o cartão microSD preparado, a rodar o DOOM a partir de um computador Linux com Docker. O jogo é o da forma `SDFS=fatfs`, que lê as WADs como arquivos de um cartão FAT32. A plataforma é a `doom` do Insper-RISC-V, com o núcleo a 100 MHz, a SDRAM a 125 MHz e a FLASH como janela da SDRAM.

## 1. O que se produz

| Arquivo | Como se obtém | Seção |
| :--- | :--- | :--- |
| `doom.bin` | `scripts/image.sh` deste repositório | 3 |
| `doom_de0_cv.sof` | `riscv-tools program --compile-only` no Insper-RISC-V | 4 |
| `scripts/load_flash.py` | Já está neste repositório | 6 |

O `.sof` e o `doom.bin` formam um par e não se misturam com arquivos de outra versão da plataforma. O `doom.bin` da forma de cartão cru (`SDFS=raw`) tem outro formato e não serve aqui.

## 2. Preparar o computador

| Item | Requisito |
| :--- | :--- |
| Docker | Funcionando, com o contexto `default` |
| Imagem | `ghcr.io/insper-riscv/dev_tools:latest`, que traz o `uv`, o Python 3.14 e o GCC RISC-V |
| Quartus Prime Lite 25.1 | Instalado em `/opt/altera_lite`, com `quartus_pgm` e `jtagconfig` no `PATH` |
| USB-Blaster | Regra udev instalada e cabo reconectado depois dela |
| Rede | Acesso a `github.com`, porque a gravação baixa o Tools do commit `f7ea593` |

A regra udev do USB-Blaster entra com este comando.

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

O Python do computador não é usado. Todo comando de Python roda dentro do container.

## 3. Montar o `doom.bin`

Com o repositório clonado, na raiz dele:

```bash
./fetch.sh
PLATFORM_DIR=platform_doom SDFS=fatfs MENU=1 VIDEO=1 INTERACTIVE=1 SD=demand ONDEMAND=1 sh scripts/image.sh
```

| Variável | Efeito |
| :--- | :--- |
| `SDFS=fatfs` | O jogo lê as WADs de um FAT32 comum no cartão, sem `wads.bin` |
| `SD=demand ONDEMAND=1` | A WAD fica só no cartão e o jogo lê cada lump quando precisa |
| `MENU=1` | A placa mostra o menu de escolha do jogo antes do DOOM |
| `VIDEO=1` | Liga a saída VGA e o framebuffer indexado |
| `INTERACTIVE=1` | Liga o teclado PS/2 e o jogo completo |
| `PLATFORM_DIR=platform_doom` | Usa o runtime da plataforma deste repositório |

A imagem sai em `build-board/doom.bin`. O `image.sh` imprime também os comandos da forma antiga de carga, com `load-ram`, que não são usados neste guia.

## 4. Compilar o `.sof`

No clone do Insper-RISC-V, com a plataforma `doom` da FLASH como janela da SDRAM, a compilação roda no container com o Quartus montado.

```bash
docker -c default run --rm --network host \
  -v /opt/altera_lite:/opt/altera_lite:ro -v "$PWD":/w -w /w \
  -e HOME=/tmp -u "$(id -u):$(id -g)" ghcr.io/insper-riscv/dev_tools:latest sh -c \
  'export PATH="$PATH:/opt/altera_lite/25.1std/quartus/bin"; \
   riscv-tools --root . --config src/cyclone-v/doom/config.yaml program --compile-only --seed 6'
```

O `.sof` sai em `build/doom-de0-cv/output_files/doom_de0_cv.sof`, depois de 7 a 10 minutos. Os relatórios da mesma pasta dizem se o timing fechou; o `doom_de0_cv.sta.summary` lista a folga de setup de cada relógio, e todas precisam ser positivas.

## 5. Preparar o cartão microSD

O cartão é FAT32 com cluster de 32 KB ou maior, e os nomes são do formato 8.3.

| No cartão | Tamanho |
| :--- | ---: |
| `DOOM.WAD` | 12.408.292 bytes |
| `DOOM2.WAD` | 14.604.584 bytes |
| `PLUTONIA.WAD` | 17.420.824 bytes |
| `TNT.WAD` | 18.195.736 bytes |
| `MASTER/` com as 20 fases e o `DOOM2.MAP` | só aparece no menu junto do `DOOM2.WAD` |

Formatar apaga o cartão inteiro.

```bash
sudo mkfs.vfat -F 32 -s 64 -n DOOM /dev/sdX1
```

O jogo lê o cartão em modo somente leitura e entende só FAT12, FAT16 e FAT32. Um cartão em exFAT aparece sem nenhum jogo no menu. O guia completo do cartão é [CARTAO-FATFS.md](CARTAO-FATFS.md). Ejetar o cartão pelo sistema antes de levá-lo à placa.

## 6. Gravar o jogo na placa

1. Conectar a fonte, o cabo USB-Blaster, o monitor VGA, o teclado PS/2 e o cartão microSD, e ligar a placa.
2. Listar os cabos.

```bash
jtagconfig
```

A DE0-CV aparece com o IDCODE `02B050DD` (`5CE(BA4|FA4)`). Guardar o nome do cabo dessa linha, por exemplo `USB-Blaster [3-6]`, porque o número muda entre computadores e depois de reiniciar. A DE2-115 tem IDCODE `020F70DD` e nunca recebe este `.sof`.

Os três passos seguintes usam o mesmo nome de cabo. Só um computador programa a placa por vez.

3. Programar o FPGA.

```bash
quartus_pgm -c "USB-Blaster [3-6]" -m jtag -o "p;build/doom-de0-cv/output_files/doom_de0_cv.sof@1"
```

4. Gravar o `doom.bin` na janela FLASH da SDRAM, a partir de `0x1000`, pela porta de depuração (instância 3 do Virtual JTAG). A gravação relê tudo para conferir, leva cerca de 5 minutos e termina com `flash: conferido`. Na raiz deste repositório:

```bash
docker -c default run --rm --network host \
  -v /opt/altera_lite:/opt/altera_lite:ro -v "$PWD":/r:ro -w /tmp \
  -e HOME=/tmp -e UV_CACHE_DIR=/tmp/uvc -e RV32_SDRAM_DEBUG_INSTANCE=3 \
  -u "$(id -u):$(id -g)" ghcr.io/insper-riscv/dev_tools:latest sh -c \
  'export PATH="$PATH:/opt/altera_lite/25.1std/quartus/bin"; \
   uv run --no-project --python 3.14 \
     --with "git+https://github.com/insper-riscv/Tools.git@f7ea593" \
     python /r/scripts/load_flash.py /r/build-board/doom.bin "USB-Blaster [3-6]"'
```

| Parte do comando | Efeito |
| :--- | :--- |
| `--network host` | O container alcança o `jtagd` do computador |
| `-v /opt/altera_lite:...:ro` | Dá ao container o `quartus_stp` que a gravação usa |
| `RV32_SDRAM_DEBUG_INSTANCE=3` | Aponta para a porta de depuração da SDRAM; sem ela a gravação não acha a porta |
| `--python 3.14` | Sem ele o `uv` escolhe o Python 3.15 e a montagem do cocotb falha |
| `@f7ea593` | Versão do Tools cuja porta de depuração combina com este `.sof` |

5. Programar o `.sof` de novo, com o mesmo comando do passo 3. A segunda programação zera o núcleo, e a BOOT salta para `0x1000`, onde o jogo já está gravado.

O menu do DOOM aparece no monitor. Escolher o jogo com as setas ou com `W` e `S`, e confirmar com `Enter`, `E` ou espaço. As teclas estão em [PLACA.md](PLACA.md), seção 12.2.

## 7. Conferir que a placa está viva

A leitura de uma palavra da SDRAM só responde se o FPGA estiver configurado.

```bash
docker -c default run --rm --network host -v /opt/altera_lite:/opt/altera_lite:ro \
  -e HOME=/tmp -e RV32_SDRAM_DEBUG_INSTANCE=3 -u "$(id -u):$(id -g)" \
  ghcr.io/insper-riscv/dev_tools:latest sh -c \
  'export PATH="$PATH:/opt/altera_lite/25.1std/quartus/bin"; \
   quartus_stp -t /opt/riscv-tools/riscv-tools/lib/python3.14/site-packages/riscv_tools/jtag/tcl/sdram_dbg.tcl \
   "USB-Blaster [3-6]" "@1: 5CE(BA4|FA4) (0x02B050DD)" read 15860736 1'
```

Se o comando falhar, o FPGA perdeu a configuração e os passos 3 a 5 da seção 6 se repetem. A configuração some a cada desligamento da placa, e a SDRAM com ela.

## 8. Se algo não funciona

| Sintoma | Causa provável | O que fazer |
| :--- | :--- | :--- |
| `jtagconfig` não lista a placa | Regra udev ausente ou cabo solto | Seção 2 e reconectar o cabo |
| `load-flash: this bitstream has no hold and CRC` | A gravação foi feita com o `riscv-tools load-flash`, que pede uma palavra `INFO` que a porta de depuração deste `.sof` não tem | Gravar com o `scripts/load_flash.py`, como na seção 6 |
| Erro `cocotb ... maximum Python version of 3.14` | O `uv` escolheu o Python 3.15 | Manter `--python 3.14` |
| `the SDRAM is not initialized` | Instância do Virtual JTAG errada | Exportar `RV32_SDRAM_DEBUG_INSTANCE=3` |
| Tela preta depois do passo 5 | Imagem não gravada ou erro na conferência | Repetir o passo 4 e ler a mensagem de conferência |
| Menu sem nenhum jogo | Cartão não é FAT32 ou não foi detectado | Seção 5 |
| Menu sem algum jogo | Nome fora do 8.3 ou `MASTER/` no lugar errado | Seção 5 |
| Jogo trava logo depois do menu | `.sof` e `doom.bin` de versões diferentes | Refazer as seções 3 e 4 do mesmo commit |

## 9. Cuidados

1. Antes de usar a placa, tomar o lock dela, para que nenhum outro processo a programe no meio dos passos. O lock é um arquivo por placa, como `/tmp/de0cv-<máquina>.lock`.

```bash
exec 9>/tmp/de0cv-laptop.lock && flock -w 60 9
```

2. Não rodar `quartus_pgm` nem `quartus_stp` de outra pessoa ao mesmo tempo.
3. Um serviço que reprograma o FPGA sozinho precisa estar parado, ou com o cabo desconectado dele, antes de usar o cabo em outro computador.
