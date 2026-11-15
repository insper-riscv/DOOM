/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linker script do DOOM em RV32IM para uma RAM executavel unica (Spike hoje,
 * SDRAM da plataforma doom depois). A origem e o comprimento da RAM sao os
 * simbolos RAM_ORIGIN e RAM_LENGTH, definidos so aqui; mude-os na linha de
 * comando do ld com --defsym=RAM_ORIGIN=... --defsym=RAM_LENGTH=...
 *
 * Tudo (codigo, constantes, WAD embutida, dados, bss, heap e pilha) fica em
 * uma regiao. Os dados iniciais tem endereco de carga igual ao de execucao,
 * entao a copia do crt0 da picolibc copia a regiao sobre ela mesma.
 *
 * Os simbolos de TLS, heap e pilha seguem o picolibc.ld.
 */

ENTRY(_start)

RAM_ORIGIN = DEFINED(RAM_ORIGIN) ? RAM_ORIGIN : 0x40000000;
RAM_LENGTH = DEFINED(RAM_LENGTH) ? RAM_LENGTH : 0x04000000;

MEMORY
{
	ram (rwx) : ORIGIN = RAM_ORIGIN, LENGTH = RAM_LENGTH
}

PHDRS
{
	text PT_LOAD;
	rodata PT_LOAD;
	ram_init PT_LOAD;
	ram PT_LOAD;
	tls_init PT_TLS;
	tls PT_TLS;
}

SECTIONS
{
	PROVIDE(__stack = ORIGIN(ram) + LENGTH(ram));

	.init : {
		KEEP (*(.text.init.enter))
		KEEP (*(.data.init.enter))
		KEEP (*(SORT_BY_NAME(.init) SORT_BY_NAME(.init.*)))
	} >ram AT>ram :text

	.text : {
		*(.literal.unlikely .text.unlikely .literal.unlikely.* .text.unlikely.*)
		*(.literal.startup .text.startup .literal.startup.* .text.startup.*)
		*(SORT(.text.sorted.*))
		*(.literal .text .literal.* .text.* .opd .opd.* .branch_lt .branch_lt.* )
		*(.gnu.linkonce.t.*)
		KEEP (*(.fini .fini.*))
		__text_end = .;
		PROVIDE (__etext = __text_end);
		PROVIDE (_etext = __text_end);
		PROVIDE (etext = __text_end);
		. = ALIGN(8);
		PROVIDE_HIDDEN ( __preinit_array_start = . );
		PROVIDE_HIDDEN ( __bothinit_array_start = . );
		KEEP (*(.preinit_array))
		PROVIDE_HIDDEN ( __preinit_array_end = . );
		PROVIDE_HIDDEN ( __init_array_start = . );
		KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
		KEEP (*(.init_array .ctors))
		PROVIDE_HIDDEN ( __init_array_end = . );
		PROVIDE_HIDDEN ( __bothinit_array_end = . );
		PROVIDE_HIDDEN ( __fini_array_start = . );
		KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
		KEEP (*(.fini_array .dtors))
		KEEP (*(.fini_array*))
		PROVIDE_HIDDEN ( __fini_array_end = . );
	} >ram AT>ram :text

	.rodata : {
		*(.rdata)
		*(.rodata .rodata.* )
		*(.gnu.linkonce.r.*)
		*(.srodata.cst16)
		*(.srodata.cst8)
		*(.srodata.cst4)
		*(.srodata.cst2)
		*(.srodata .srodata.*)
	} >ram AT>ram :rodata

	/* Imagem da WAD, embutida so no build do Spike; na placa a WAD vem por JTAG */
	.wad : ALIGN(16) {
		*(.wad)
	} >ram AT>ram :rodata

	.data.rel.ro : {
		*(.data.rel.ro .data.rel.ro.*)
	} >ram AT>ram :rodata

	.plt : {
		*(.rela.iplt)
		*(.rela.*)
	} >ram AT>ram :rodata

	PROVIDE( __plt_start = ADDR(.plt) );
	PROVIDE( __plt_size = SIZEOF(.plt) );

	.got : {
		*(.got.plt)
		*(.got)
	} >ram AT>ram :rodata

	.toc : {
		*(.toc .toc.*)
		*(.iplt .iplt.*)
	} >ram AT>ram :rodata

	/* Mailbox HTIF do Spike (tohost e fromhost), alinhado a 64 bytes */
	.tohost : ALIGN(64) {
		*(.tohost)
		. = ALIGN(64);
		*(.fromhost)
	} >ram AT>ram :ram_init

	.data :
		ALIGN_WITH_INPUT
	{
		*(.data .data.* )
		*(.gnu.linkonce.d.*)
		. = ALIGN(8);
		PROVIDE( __global_pointer$ = . + 0x800 );
		PROVIDE( _gp = . + 0x8000);
		PROVIDE (_SDA_BASE_ = .);
		*(.sdata .sdata.* .sdata2.* )
		*(.gnu.linkonce.s.*)
		*(.gnu.linkonce.l*)
		*(.scommon*)
		*(.lit*)
		PROVIDE( __tls_head_start = .);
		KEEP (*(.stack_chk .stack_chk.*))
		PROVIDE( __tls_head_end = .);
		PROVIDE( __tls_inittls = 0 );
	} >ram AT>ram :ram_init

	PROVIDE(__data_start = ADDR(.data));
	PROVIDE(__data_source = LOADADDR(.data));

	.tdata :
		ALIGN_WITH_INPUT
	{
		PROVIDE( __tls_first = . );
		*(.tdata .tdata.* .gnu.linkonce.td.*)
		PROVIDE( __data_end = . );
		PROVIDE( __tdata_end = . );
	} >ram AT>ram :tls_init :ram_init

	.tbss (NOLOAD) : {
		*(.tbss .tbss.* .gnu.linkonce.tb.*)
		*(.tcommon)
		PROVIDE( __tls_end = . );
		PROVIDE( __tbss_end = . );
	} >ram AT>ram :tls :ram

	.tbss_space (NOLOAD) : {
		. = ADDR(.tbss);
		. = . + SIZEOF(.tbss);
	} >ram AT>ram :ram

	PROVIDE( __tls_start = SIZEOF(.tdata) ? ADDR(.tdata) : ADDR(.tbss) );
	PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
	PROVIDE( __tls_head_size = ALIGN ( __tls_head_end - __tls_head_start, __tls_align ) );
	PROVIDE( __tdata_start = ADDR(.tdata) - __tls_head_size );
	PROVIDE( __tdata_source = LOADADDR(.tdata) - __tls_head_size );
	PROVIDE( __tdata_source_end = LOADADDR(.tdata) + SIZEOF(.tdata) );
	PROVIDE( __tdata_size = SIZEOF(.tdata) + __tls_head_size );
	PROVIDE( __data_source_end = LOADADDR(.tdata) + SIZEOF(.tdata) );
	PROVIDE( __edata = __data_end );
	PROVIDE( _edata = __data_end );
	PROVIDE( edata = __data_end );
	PROVIDE( __data_size = __data_end - __data_start );
	PROVIDE( __data_source_size = __data_source_end - __data_source );

	.bss (NOLOAD) : {
		. = ALIGN( __tls_align );
		PROVIDE( __tls_tail_start = . );
		KEEP (*(.tls_tail_extra .tls_tail_extra.*))
		PROVIDE( __tls_tail_end = . );
		PROVIDE( __non_tls_bss_start = . );
		*(.sbss* )
		*(.gnu.linkonce.sb.*)
		*(.bss .bss.* )
		*(.gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(8);
		__bss_end = .;
		_end = .;
	} >ram AT>ram :ram

	PROVIDE( __tls_space_size = __tls_end - __tls_start );
	PROVIDE( __tls_space = __tls_start );
	PROVIDE( __bss_start = ADDR(.tbss) );
	PROVIDE( __tbss_start = ADDR(.tbss) );
	PROVIDE( __tbss_offset = ADDR(.tbss) - __tls_start );
	PROVIDE( __tbss_size = SIZEOF(.tbss) );
	PROVIDE( __tls_space_pad = ALIGN ( __tls_space_size, __tls_align ) - __tls_space_size );
	PROVIDE( __tls_tail_size = __tls_space_pad + (__tls_tail_end - __tls_tail_start) );
	PROVIDE( __tls_size = __tls_head_size + __tls_space_size + __tls_tail_size );
	PROVIDE( __tls_base = __tls_space - __tls_head_size );
	PROVIDE( __end = _end );
	PROVIDE( end = _end );
	PROVIDE( __bss_size = __bss_end - __bss_start );

	/* O resto da RAM vira heap, menos a pilha no topo */
	PROVIDE( __heap_start = __end );
	PROVIDE( __heap_end = __stack - (DEFINED(__stack_size) ? __stack_size : 0x00010000) );
	PROVIDE( __heap_size = __heap_end - __heap_start );

	PROVIDE ( __stack_bottom = ORIGIN(ram) + LENGTH(ram) - (DEFINED(__stack_size) ? __stack_size : 0x00010000) );

	.stack __stack_bottom (NOLOAD) : {
		. += (DEFINED(__stack_size) ? __stack_size : 0x00010000);
	} >ram :ram

	/DISCARD/ : {
		*(.note .note.*)
		*(.eh_frame .eh_frame.*)
	}

	.comment       0 : { *(.comment) }
	.debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
	.debug_str      0 : { *(.debug_str) }
	.gnu.attributes 0 : { KEEP (*(.gnu.attributes)) }
}

ASSERT( __data_size == __data_source_size,
    "ERROR: .data/.tdata load size does not match RAM size");
