ENTRY(_start)

SECTIONS {
    . = 0x40080000;

    .text : {
        *(.text.start)
        *(.text)
        *(.text.*)
    }

    . = ALIGN(8);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(8);
    .data : {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(8);
    __bss_start = .;
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
    }
    . = ALIGN(8);
    __bss_end = .;

    . = ALIGN(16);
    . += 0x4000;
    _stack_top = .;

    . = ALIGN(8);
    _end = .;
}
