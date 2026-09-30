/* 用户程序链接脚本：链接基址 0x48000000。
   实际加载槽位不同也没关系：代码使用 PC 相对寻址（ADRP/BL），
   入口 PC 和栈指针由内核按槽位基址重设。 */
ENTRY(_start)

SECTIONS {
    . = 0x48000000;

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
}
