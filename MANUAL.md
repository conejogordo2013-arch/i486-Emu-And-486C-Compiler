# Manual de uso del Toolchain i486 / 486CC

Este repositorio contiene una suite freestanding en C++17 para crear, ensamblar, linkear y ejecutar programas de clase Intel i486 sin depender de BIOS ni sistema operativo externo.

## 1. Componentes

- **CPU i486 / x87**: registros generales y de segmento, `EIP`, `EFLAGS`, `CR0..CR4`, prefijos, ModRM/SIB, interrupciones, paging/TLB, subset ampliado de ISA i486 y FPU x87.
- **Memoria**: real mode, protected mode simplificado, GDT/LDT, descriptores empaquetables, protecciones, fallos de página y accesos desalineados/cross-page.
- **Dispositivos**: PIC 8259, PIT 8254, DMA 8237, RTC/CMOS, teclado PS/2, serial, paralelo, VGA, SB16 y storage ATA-like.
- **486CC**: compilador C-like a ASM i486.
- **486AS**: assembler Intel-like de dos pasadas con símbolos y relocaciones.
- **486LD**: linker plano para imágenes cargables en `0x7C00`.
- **`c486tool`**: CLI unificada.

## 2. Construcción

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Atajo con Make:

```sh
make test
```

## 3. CLI `c486tool`

```sh
# Compilar 486CC a ASM
build/c486tool cc examples/486cc/arithmetic.c486 build/arithmetic.asm

# Ensamblar ASM a binario flat
build/c486tool as examples/asm/arithmetic.asm build/arithmetic.bin

# Compilar + ensamblar + linkear 486CC
build/c486tool build examples/486cc/arithmetic.c486 build/arithmetic.bin

# Ejecutar imagen flat en el emulador
build/c486tool run build/arithmetic.bin 1000
```

La imagen flat se carga en `0x7C00`, el mismo punto usado por el flujo de boot del emulador.

## 4. 486CC

### Sintaxis soportada

- Tipos: `void`, `bool`, `char`, `int`, `float`, `double`; punteros y `struct` están representados en el sistema de tipos/parser.
- Funciones con stack frame clásico:

```asm
push ebp
mov ebp, esp
sub esp, N
...
mov esp, ebp
pop ebp
ret
```

- Variables locales, asignaciones, llamadas, `return`, bloques, `if/else`, `while`, `for`.
- Operadores: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&`, `|`, `^`, `&&`, `||`, `!`, `~`.
- Literales enteros decimales y hexadecimales (`0x...`).
- Inline assembly con `asm { ... }`.

### Ejemplo de 486CC avanzado

```c
int main(){
    int sum = 0;
    for (int i = 0; i < 4; i = i + 1) {
        sum = sum + i;
    }
    asm { nop; out 0x80, al; }
    return (sum & 7) | 8;
}
```

El bloque `asm {}` se copia como instrucciones del assembler integrado, por lo que puede usar puertos, instrucciones i486 y etiquetas locales del archivo generado si se escriben cuidadosamente.

## 5. 486AS

### Directivas

- `bits 32`
- `section .text`
- `global nombre`
- `label:`
- `db`, `dw`, `dd`
- `align N`
- `org OFFSET`

`db` acepta enteros y cadenas simples:

```asm
db "HELLO", 0
```

### Direccionamiento

- Absoluto: `[0xB8000]`
- Base: `[eax]`, `[ebx+16]`, `[ebp-4]`, `[esp]`
- Tamaños: `byte [mem]`, `dword [mem]`

### Instrucciones principales

- Control: `jmp`, `Jcc` amplio (`je`, `jne`, `jl`, `jge`, `ja`, `jbe`, etc.), `call`, `ret`, `ret imm16`, `int`, `iret`, `hlt`, `nop`, `cli`, `sti`.
- Stack: `push r32`, `push imm32`, `pop r32`.
- Movimiento: `mov r32, imm32`, `mov r8, imm8`, `mov r/m32, r32`, `mov r32, r/m32`, `mov r/m8, r8`, `mov r8, r/m8`, `mov r/m32, imm32`, `mov r/m8, imm8`, `movzx`.
- ALU: `add`, `sub`, `cmp`, `and`, `or`, `xor`, `test`, `neg`, `not`.
- Mul/div: `imul r32, r/m32`, `mul`, `div`, `idiv`, `cdq`.
- Shifts/rotates básicos: `shl/sal`, `shr`, `sar`, `rol`, `ror` con cuenta 1.
- i486: `cpuid`, `bswap`, `cmpxchg`, `xadd`, `bt`, `bts`, `btr`, `btc`.
- I/O: `in al, imm8`, `in al, dx`, `in eax, dx`, `out imm8, al`, `out dx, al`, `out dx, eax`.

## 6. Linker plano

`Linker486` concatena secciones `.text`, construye una tabla de símbolos globales/locales y aplica relocaciones absolutas o relativas de 32 bits. El formato de salida es un binario flat pensado para carga directa en memoria.

## 7. Hardware emulado

| Dispositivo | Puertos | IRQ | Estado |
|---|---:|---:|---|
| PIC 8259 | `0x20/0x21`, `0xA0/0xA1` | N/A | Máscaras IRR/ISR básicas y EOI. |
| PIT 8254 | `0x40..0x43` | 0 | Contador/reload simplificado. |
| DMA 8237 | `0x00..0x0F`, `0x80..0x8F` | N/A | Registros de canal/página. |
| RTC/CMOS | `0x70/0x71` | 8 | Índice CMOS, hora UTC simplificada. |
| Teclado PS/2 | `0x60/0x64` | 1 | Cola de scancodes. |
| Serial COM1 | `0x3F8..0x3FF` | 4 | RX/TX FIFO y registros 16550-like básicos. |
| Paralelo LPT1 | `0x378..0x37A` | N/A | Data/status/control y buffer de salida. |
| VGA | `0x3C0..0x3DA` | N/A | Texto 80x25 y framebuffer 320x200. |
| SB16 | `0x220..0x22F` | 5 | FIFO PCM unsigned 8-bit. |
| Storage | `0x1F0..0x1F7` | 14 | Lectura de sectores de 512 bytes. |

## 8. Validación

La suite `i486_tests` cubre CPU/ALU/control-flow, ModRM/SIB, memoria, paging/TLB, interrupciones, dispositivos, compilador, assembler, linker, inline asm, bitwise, `for`, operaciones i486 (`cpuid`, `xadd`, `cmpxchg`, `bt/bts`) y x87 (`fld`, `fsqrt`, `fstp`).

## 9. Limitaciones conocidas

El diseño apunta a fidelidad progresiva. Una reproducción absolutamente perfecta del Intel i486 requiere todavía completar todos los opcodes documentados, gates protegidos exactos, task switching real, PIC/PIT/DMA ciclo-a-ciclo, cachés con políticas completas, prefetch/pipeline, FPU x87 completa y comparación contra suites externas/documentación Intel.
