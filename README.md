# i486-Emu-And-486C-Compiler

Suite C++17 freestanding para experimentar con software de clase Intel i486: emulador PC, CPU i486, FPU x87, memoria segmentada/paginada, dispositivos básicos, compilador 486CC, assembler 486AS, linker plano y CLI `c486tool`.

> Estado actual: el proyecto es una base ejecutable y probada. No afirma ser todavía una réplica bit-exact/cycle-exact completa de todos los steppings reales del Intel i486; cada cambio mantiene interfaces para cerrar esas brechas con pruebas incrementales.

## Mapa del toolchain

```text
.c486 source
   |  Lexer + Parser + IR + Optimizer
   v
486CC  ---------------------> Intel-like ASM i486
                                  |
.asm source ----------------------+
                                  v
486AS assembler  ------------> ObjectFile (.text + symbols + relocations)
                                  |
                                  v
486LD linker ----------------> flat binary loaded at 0x7C00
                                  |
                                  v
PC486Emulator ---------------> CPU + Memory + I/O devices
```

## Componentes principales

| Componente | Archivos | Descripción |
|---|---|---|
| CPU i486 | `include/i486/cpu.hpp`, `src/cpu.cpp` | Registros GPR/segmento/control, EFLAGS, ModRM/SIB, prefijos, ALU, control-flow, interrupciones, subconjunto i486 ampliado y x87. |
| Memoria | `include/i486/memory.hpp`, `src/memory.cpp` | Real/protected mode, GDT/LDT simplificadas, descriptores x86, paging, TLB, PageFault, accesos cross-page. |
| Bus I/O | `include/i486/bus.hpp`, `src/bus.cpp` | Registro de dispositivos por puerto, IN/OUT y entrega de IRQs pendientes. |
| Dispositivos | `include/i486/devices.hpp`, `src/devices.cpp` | PIC 8259, PIT 8254, DMA 8237, RTC/CMOS, VGA, SB16, teclado PS/2, ATA-like storage, serial 16550-like y paralelo. |
| Emulador PC | `include/i486/emulator.hpp`, `src/emulator.cpp` | Integra CPU, memoria, bus y dispositivos; carga boot sector en `0x7C00`. |
| 486CC | `include/c486cc/compiler.hpp`, `src/compiler.cpp` | Lenguaje C-like con funciones, locals, if/while/for, expresiones, llamadas, bitwise e `asm {}`. |
| 486AS/486LD | `src/assembler.cpp` | Assembler y linker planos con símbolos, relocaciones y directivas de datos/alineación. |
| CLI | `src/toolchain_cli.cpp` | Comandos `cc`, `as`, `build` y `run`. |

## Construcción y pruebas

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

También se puede usar:

```sh
make test
```

## Uso de `c486tool`

```sh
# 486CC -> ASM
build/c486tool cc examples/486cc/arithmetic.c486 build/arithmetic.asm

# ASM -> flat binary
build/c486tool as examples/asm/arithmetic.asm build/arithmetic.bin

# 486CC -> ASM -> ObjectFile -> flat binary
build/c486tool build examples/486cc/arithmetic.c486 build/arithmetic.bin

# Ejecutar imagen en el emulador
build/c486tool run build/arithmetic.bin 1000
```

## 486CC: lenguaje soportado

- Tipos: `void`, `bool`, `char`, `int`, `float`, `double`, punteros sintácticos y `struct` sintáctico.
- Funciones con convención tipo cdecl: parámetros desde `[ebp+8]`, retorno en `eax`.
- Sentencias: bloques, variables locales, asignaciones, llamadas, `return`, `if/else`, `while`, `for`.
- Operadores: aritméticos `+ - * / %`, comparaciones `== != < <= > >=`, bitwise `& | ^`, lógicos `&& ||` como comparaciones IR, unarios `- ! ~`.
- Inline assembly: `asm { ... }` inserta instrucciones 486AS directamente en el backend.

Ejemplo:

```c
int main(){
    int acc = 0;
    for (int i = 0; i < 4; i = i + 1) {
        acc = acc + i;
    }
    asm { nop; out 0x80, al; }
    return (acc & 7) | 8;
}
```

## 486AS: directivas e instrucciones

Directivas:

- `bits 32`, `section .text`, `global symbol`
- Etiquetas `name:`
- Datos `db`, `dw`, `dd`; `db` acepta cadenas simples entre comillas.
- `align N`, `org OFFSET` para relleno dentro de la imagen plana.

Memoria:

- Absoluta: `[0xB8000]`
- Base: `[eax]`, `[ebx+16]`, `[ebp-4]`, `[esp]`
- Tamaños: `byte [mem]`, `dword [mem]`

Instrucciones cubiertas por el assembler integrado:

- Control: `jmp`, matriz amplia `Jcc`, `call`, `ret`, `ret imm16`, `int`, `iret`, `hlt`, `nop`, `cli`, `sti`.
- Stack: `push`, `pop`.
- Movimiento: `mov` r32/r8/imm/mem, `movzx`.
- ALU: `add`, `sub`, `cmp`, `and`, `or`, `xor`, `test`, `neg`, `not`.
- Mul/div: `imul`, `mul`, `div`, `idiv`, `cdq`.
- Shifts/rotates básicos: `shl/sal`, `shr`, `sar`, `rol`, `ror` con cuenta 1.
- i486: `cpuid`, `bswap`, `cmpxchg`, `xadd`, `bt`, `bts`, `btr`, `btc`.
- I/O: `in`, `out` para puertos inmediatos o `dx` con `al/eax`.

## Emulador y hardware

El emulador instala estos dispositivos en el `IOBus`:

- PIC 8259 maestro/esclavo: `0x20/0x21`, `0xA0/0xA1`.
- PIT 8254: `0x40..0x43`, IRQ0.
- DMA 8237: `0x00..0x0F`, páginas `0x80..0x8F`.
- RTC/CMOS: `0x70/0x71`, IRQ8 simplificada.
- Teclado PS/2: `0x60/0x64`, IRQ1.
- Serial 16550-like COM1: `0x3F8..0x3FF`, IRQ4.
- Paralelo LPT1-like: `0x378..0x37A`.
- VGA texto/gráfico: memoria `0xB8000`/`0xA0000`, puertos `0x3C0..0x3DA`.
- SB16 simplificado: `0x220..0x22F`, IRQ5.
- Storage ATA-like: `0x1F0..0x1F7`, IRQ14.

## Precisión del i486

La CPU incluye registros generales, segmentos, `EIP`, `EFLAGS`, control `CR0..CR4`, x87, prefijos, ModRM/SIB, interrupciones reales, una IDT simplificada en protected mode, paging con TLB y excepciones de página. El objetivo de diseño es modularidad y pruebas: completar una ISA i486 absolutamente perfecta requiere seguir ampliando tablas de opcodes, gates del modo protegido, FPU x87 completa, timing ciclo-a-ciclo, cachés y comportamiento exacto de chipset.
