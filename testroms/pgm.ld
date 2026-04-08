MEMORY
{
  rom      (rx)  : ORIGIN = 0x00000000, LENGTH = 0x00020000
  ram      (rwx) : ORIGIN = 0x00800000, LENGTH = 0x00020000
}

STACK_SIZE = 0x2000;

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors .vectors.*));
        *(.text .text.*);
        *(.rodata .rodata.*);
        . = ALIGN(4);
        _sctors = .;
        KEEP(*(SORT(.ctors.*)));
        KEEP(*(.ctors));
        . = ALIGN(4);
        _ectors = .;
        _etext = .;
    } > rom

    . = .;

    .bss (NOLOAD) :
    {
        KEEP(*(.sprite_buffer .sprite_buffer.*));
        _sbss = . ;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(2);
        _ebss = . ;
    } > ram

    .data :
    {
        _sdata = .;
        *(.data*);
        . = ALIGN(2);
        _edata = .;
    } > ram AT >rom

    .stack (NOLOAD):
    {
        . = ALIGN(2);
        _sstack = .;
        . = . + STACK_SIZE;
        . = ALIGN(2);
        _estack = .;
    } > ram

    . = ALIGN(2);
    _end = . ;
}
