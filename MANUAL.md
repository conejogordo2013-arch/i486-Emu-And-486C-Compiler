# Manual de uso del Toolchain i486 / 486CC

Este proyecto contiene un MVP estable de toolchain freestanding para i486:

- Emulador de CPU i486 de 32 bits con memoria, segmentación/paginación simplificada, IRQs y dispositivos básicos.
- 486CC: compilador C-like (`.c486`) a ensamblador Intel 32-bit compatible con i486.
- 486AS: ensamblador de dos pasadas integrado (`Assembler486`) para programas `.asm` pequeños/medianos.
- 486LD: linker plano (`Linker486`) que resuelve símbolos/relocaciones relativas y produce binarios flat.
- `c486tool`: CLI unificada para compilar, ensamblar, linkear y ejecutar imágenes en el emulador.

> Alcance: el objetivo es una base v1.0 funcional y mantenible. No depende de BIOS/OS externo y evita instrucciones posteriores a i486.

## Compilar el proyecto

```sh
make test
```

Esto configura CMake, compila la librería, el ejecutable de pruebas y `c486tool`, y ejecuta CTest.

## CLI: `c486tool`

Después de compilar, el binario queda en `build/c486tool`.

### Compilar 486C a ASM

```sh
build/c486tool cc examples/486cc/arithmetic.c486 build/arithmetic.asm
```

### Ensamblar ASM a binario flat

```sh
build/c486tool as examples/asm/arithmetic.asm build/arithmetic.bin
```

### Compilar + ensamblar + linkear 486C

```sh
build/c486tool build examples/486cc/arithmetic.c486 build/arithmetic.bin
```

### Ejecutar una imagen flat

```sh
build/c486tool run build/arithmetic.bin 1000
```

La imagen se carga en `0x7C00`, igual que el flujo de arranque usado por las pruebas.

## 486AS: sintaxis soportada

Directivas:

- `bits 32`
- `section .text`
- `global nombre`
- Etiquetas: `label:`
- Datos: `db`, `dw`, `dd`

Instrucciones principales:

- Control: `jmp`, `je`, `jne`, `call`, `ret`, `hlt`, `nop`, `int imm8`
- Stack: `push r32`, `push imm32`, `pop r32`
- Movimiento: `mov r32, imm32`, `mov r32, r32`, `mov r32, [mem]`, `mov [mem], r32`, `mov [mem], imm32`
- ALU: `add`, `sub`, `cmp`
- Multiplicación/división: `imul r32, r/m32`, `cdq`, `idiv r/m32`
- Condiciones: `sete/setz`, `setne/setnz`, `setl`, `setle`, `setg`, `setge`
- Extensión: `movzx r32, r8`

Memoria soportada por el assembler:

- Absoluta: `[0xB8000]`
- Base simple: `[ebp-4]`, `[ebp+8]`, `[eax]`, `[ebx+16]`

## 486CC: lenguaje soportado

El compilador acepta un subconjunto C-like suficiente para programas freestanding:

- Tipos primitivos: `void`, `bool`, `char`, `int`, `float`, `double`
- Punteros y structs en el parser/tipos base
- Funciones con parámetros cdecl en stack
- Variables locales, asignaciones, llamadas, `return`, bloques, `if/else`, `while`
- Operadores: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`

El backend genera Intel ASM 32-bit con frame clásico:

```asm
push ebp
mov ebp, esp
sub esp, N
...
mov esp, ebp
pop ebp
ret
```

## Runtime y headers `.h486`

Los headers freestanding están en `runtime/486cc/include` y existen tanto en formato `.h` como `.h486`:

- `stdint.h486`
- `stdbool.h486`
- `string.h486`
- `stdlib.h486`
- `stdio.h486`

También hay ejemplos de headers específicos en `examples/headers`.

## Dispositivos emulados

- PIT 8254 básico: puertos `0x40..0x43`, IRQ0.
- VGA texto 80x25 y framebuffer 320x200: memoria `0xB8000` / `0xA0000`, puertos `0x3C0..0x3DA`.
- Teclado PS/2 básico: puerto `0x60`, IRQ1.
- SB16 simplificado: DSP/data FIFO, IRQ5.
- Bloque/boot storage: sectores de 512 bytes y carga de boot sector.

## Flujo recomendado

1. Escribir ASM en `examples/asm/*.asm` o 486C en `examples/486cc/*.c486`.
2. Usar `build/c486tool as` o `build/c486tool build`.
3. Ejecutar con `build/c486tool run`.
4. Si algo falla, correr `make test` para validar CPU, memoria, dispositivos, compilador y assembler.

## Limitaciones conocidas de v1.0

- La emulación apunta a compatibilidad funcional de MVP, no a timing exacto ciclo-a-ciclo.
- El assembler cubre el set necesario para el backend y ejemplos del proyecto, y rechaza instrucciones no implementadas con `CompileError` explícito.
- El runtime freestanding es mínimo; libc completa, filesystem y syscalls quedan fuera de alcance del entorno bare-metal.
