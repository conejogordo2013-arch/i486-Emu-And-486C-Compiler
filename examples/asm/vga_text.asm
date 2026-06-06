; Escribe "OK" en la primera celda VGA texto (carácter + atributo) y detiene la CPU.
bits 32
global _start
_start:
    mov dword [0xB8000], 0x074B074F
    hlt
