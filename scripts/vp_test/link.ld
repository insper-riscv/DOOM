/* link.ld -- programa de teste na SDRAM: codigo e dados a partir da base da RAM (0x40000000), pilha abaixo do
 * buffer de stdout reservado pela plataforma (0x43FFFBE0). */
ENTRY(_start)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) *(.srodata*) }
    .data : { *(.data*) *(.sdata*) }
    . = ALIGN(4);
    __bss_start = .;
    .bss : { *(.bss*) *(.sbss*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;
    __stack_top = 0x43FFF000;
}
