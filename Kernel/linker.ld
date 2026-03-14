PHDRS {
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

ENTRY(KernelEntry)

SECTIONS
{
    . = 0xFFFFFFFF80000000;

    .text :
    {
        KEEP(*(.text.entry))   /* KernelEntry first */
        *(.text*)              /* rest of code */
    } :text

    .rodata :
    {
        *(.rodata*)
    } :text

    .data :
    {
        *(.data*)
    } :data

    .bss :
    {
        *(.bss*)
        *(COMMON)
    } :data
}