# DOOM no Insper-RISC-V

[English](README.en.md)

Porta do DOOM original (shareware, `doom1.wad`) para o processador RV32IM em pipeline do projeto [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V), na placa DE0-CV, com saída VGA e entrada por teclado PS/2.

## 1. O que este repositório contém

Só código escrito para este projeto, sob Apache 2.0.

| Item | Função |
| :--- | :--- |
| `fetch.sh` | Baixa a `doomgeneric` em um commit fixado e o WAD shareware, e confere o SHA-256 de cada um |
| `build.sh` | Compila a porta para `rv32im` com `ilp32` no container `dev_tools` |
| `run.sh` | Executa o `-timedemo demo1` no Spike e confere o CRC-32 do último quadro |
| `port/` | Camada de plataforma (`DG_*`), backend de WAD em memória, runtime e linker script |
| `scripts/` | Funções comuns dos scripts |
| `docs/` | Documentação de build e do gabarito, em `pt-br` e `en` |

## 2. Uso rápido

Requisitos no host: `git`, `curl`, `sha256sum` e `docker`.

```bash
./fetch.sh && ./build.sh && ./run.sh
```

O `build.sh` e o `run.sh` executam dentro do container `ghcr.io/insper-riscv/dev_tools:latest`. A execução termina com `CRC-32 confere com o gabarito: 67371a4f`. Os detalhes estão em [docs/pt-br/BUILD.md](docs/pt-br/BUILD.md).

## 3. Licenças

O código deste repositório é Apache 2.0 (arquivo `LICENSE`). A `doomgeneric` e o código do DOOM são GPL-2.0 e não ficam neste repositório. O `fetch.sh` os baixa na compilação.

O binário resultante junta o jogo e a camada de plataforma e por isso é GPL-2.0. Quem o distribuir deve incluir o texto da GPL-2.0 e a oferta do código-fonte correspondente. Alterações na `doomgeneric`, se forem necessárias, entram como arquivos `.patch` sob GPL-2.0.

O `doom1.wad` shareware tem licença própria, que permite redistribuir apenas o pacote original sem modificação. Ele também não fica neste repositório.

## 4. O que falta para a placa

Esta versão roda só no Spike, com a WAD embutida no ELF e o console pelo HTIF. A etapa D1 do plano ainda precisa de:

1. Console pela JTAG UART no lugar do HTIF.
2. WAD na SDRAM, acima do binário, carregada por `riscv-tools load-ram`.
3. Relógio por `DG_GetTicksMs` com o `time` do Core (`doom_clock.c`).
4. Plataforma `doom` do Insper-RISC-V, com `RAM` executável na SDRAM.

## 5. Contribuição

Cada alteração entra por Pull Request para a `main`. As regras gerais da organização estão em [Insper-RISC-V](https://github.com/insper-riscv/Insper-RISC-V/blob/main/CONTRIBUTING.md).
