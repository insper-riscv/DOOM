/* start.S -- entrada do programa de teste de video e teclado, ligado no inicio da SDRAM (0x40000000).
 *
 * O carregador da FLASH (banca A) espera a palavra `launch`, executa fence.i e salta aqui. Este codigo posiciona
 * a pilha, zera o .bss, chama main e, se ela voltar, estaciona o nucleo no laco da BOOT_MEMORY (rv32_wait_restart).
 */
    .section .text.start
    .global _start
_start:
    la   sp, __stack_top
    la   t0, __bss_start
    la   t1, __bss_end
1:  bgeu t0, t1, 2f
    sw   x0, 0(t0)
    addi t0, t0, 4
    j    1b
2:  call main
    li   t0, 0x100              /* rv32_wait_restart da plataforma doom (platform.yaml boot.wait_restart) */
    jr   t0
