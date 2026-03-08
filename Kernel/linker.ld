PHDRS {
  text PT_LOAD FLAGS(5); /* read+execute */
  data PT_LOAD FLAGS(6); /* read+write */
}

SECTIONS
{
    .text : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    .data : { *(.data*) } :data
    .bss : { *(.bss*) *(COMMON) } :data
}