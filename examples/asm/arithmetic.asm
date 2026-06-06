; Programa bootable mínimo: calcula 1+2, valida con JNE y deja EBX con marca de éxito.
bits 32
global _start
_start:
    mov eax, 1
    add eax, 2
    cmp eax, 3
    jne failed
    mov ebx, 0x12345678
    hlt
failed:
    mov ebx, 0
    hlt
