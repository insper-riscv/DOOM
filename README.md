# DOOM no Insper-RISC-V

[English](README.en.md)

Porta do DOOM original (shareware, `doom1.wad`) para o processador RV32IM em pipeline do projeto [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V), na placa DE0-CV, com saída VGA e entrada por teclado PS/2.

## 1. O que este repositório contém

Só código escrito para este projeto, sob Apache 2.0.

| Item | Função |
| :--- | :--- |
| `fetch.sh` | Baixa a `doomgeneric` em um commit fixado e o WAD shareware, e confere o SHA-256 de cada um |
| Camada de plataforma | Vídeo, teclado, relógio e acesso ao WAD em memória, no lugar do sistema operacional |
| Linker e runtime | Mapa de memória da plataforma `doom` e inicialização |
| Scripts de build | Compilação com o toolchain do container `dev_tools` |

## 2. Licenças

O código deste repositório é Apache 2.0 (arquivo `LICENSE`). A `doomgeneric` e o código do DOOM são GPL-2.0 e não ficam neste repositório. O `fetch.sh` os baixa na compilação.

O binário resultante junta o jogo e a camada de plataforma e por isso é GPL-2.0. Quem o distribuir deve incluir o texto da GPL-2.0 e a oferta do código-fonte correspondente. Alterações na `doomgeneric`, se forem necessárias, entram como arquivos `.patch` sob GPL-2.0.

O `doom1.wad` shareware tem licença própria, que permite redistribuir apenas o pacote original sem modificação. Ele também não fica neste repositório.

## 3. Contribuição

Cada alteração entra por Pull Request para a `main`. As regras gerais da organização estão em [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.md).
