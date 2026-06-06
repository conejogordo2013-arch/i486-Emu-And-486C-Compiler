# Emulador PC Intel i486 en C++

Este proyecto implementa una base de emulador PC i486 en C++17. La arquitectura está dividida en CPU/FPU, memoria, bus de I/O, dispositivos, arranque y motor central. No hay código Python en la implementación actual. Se puede compilar con CMake directamente o con el `Makefile` incluido (`make`, `make test`, `make run`, `make clean`).

## CPU/FPU

La CPU (`CPU486`) contiene los registros `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, `ESP`, `EBP`, `EIP`, segmentos `CS`, `DS`, `ES`, `FS`, `GS`, `SS`, y control `CR0..CR4`. `EFlags` modela `CF`, `PF`, `AF`, `ZF`, `SF`, `TF`, `IF`, `DF`, `OF`, `IOPL`, `NT`, `RF`, `VM` y `AC`, con empaquetado para interrupciones e `IRET`.

El ciclo de ejecución es `service_pending_irq -> fetch -> decode -> execute -> cycles`. El decodificador soporta prefijos de tamaño/segmento, instrucciones de movimiento, ALU de 8/32 bits, stack, saltos cortos y near para toda la matriz Jcc, llamadas/retornos, flags, `INT`, `IRET`, `IN`, `OUT`, `HLT`, acceso a `CR0` y un conjunto inicial x87 (`FLD1`, `FLDZ`, `FADD`, `FMUL`, `FDIV`). También incluye ModR/M y SIB de 32 bits para registros y memoria lineal.

La FPU x87 mantiene una pila de 8 entradas y operaciones de carga, almacenamiento y aritmética básica.

## Memoria

`Memory` reserva 64 MiB de RAM física. Las direcciones se resuelven como `selector:offset -> lineal -> física`.

- En real mode aplica `(selector << 4) + offset` y límite de 64 KiB por segmento.
- En protected mode consulta una GDT simplificada con `base`, `limit`, `present`, `writable`, `executable` y `DPL`.
- La paginación opcional usa un mapa de páginas lineales a frames físicos con flags `present`, `writable` y `user`.

Los accesos inválidos generan `MemoryFault`.

## Bus I/O y dispositivos

`IOBus` registra dispositivos por puerto, enruta `IN/OUT`, acumula IRQs pendientes y llama `tick(cycles)` a cada dispositivo tras cada instrucción.

Cada dispositivo implementa la interfaz C++ `PortDevice`: lista de puertos, lectura, escritura, tick e IRQ pendiente.

Dispositivos incluidos:

- `PIT8254`: timer simplificado que genera IRQ0.
- `VGADevice`: texto 80x25 desde `0xB8000` y gráfico 320x200 desde `0xA0000`.
- `SB16Device`: FIFO PCM unsigned 8-bit, volumen, canales, buffer host e IRQ5.
- `KeyboardDevice`: scancodes por puertos `0x60/0x64` e IRQ1.
- `BlockStorageDevice`: lectura de sectores por puertos tipo HDD en `0x1F0..0x1F7` e IRQ14.

## Interrupciones y arranque

`INT n` y las IRQs guardan `EFLAGS`, `CS` y `EIP` en la pila, limpian `IF` y resuelven el vector en IVT real mode o IDT simplificada protected mode. `IRET` restaura el estado.

`PC486Emulator::boot()` inicializa real mode, instala vectores reales básicos, carga el sector 0 en `0x7C00` si hay almacenamiento y deja la CPU lista para ejecutar el bootloader.

## Motor central

`PC486Emulator` integra memoria, bus, CPU, PIT, VGA, SB16, teclado y almacenamiento. Cada iteración ejecuta una instrucción, sincroniza dispositivos por ciclos y entrega IRQs en la siguiente instrucción si `IF=1`.

## Límites técnicos actuales

La implementación es C++ y ejecutable, con Makefile y pruebas integradas. Aun así, una réplica bit-exacta de un i486 real exige validar miles de combinaciones de opcodes, excepciones exactas, TLB, descriptor tables completas, VGA planar real, DMA/PIC/PIT fieles y SB16 DSP completo contra documentación y suites de compatibilidad. Esta versión aumenta la cobertura práctica del núcleo y mantiene una arquitectura preparada para seguir cerrando brechas de precisión.

## Hoja de ruta de precisión i486 añadida

La implementación actual no pretende declarar terminada la réplica completa de un Intel i486; en su lugar convierte el núcleo en una base verificable para cerrar brechas de fidelidad de forma incremental. El mapa de integración queda así:

```text
+------------------+      +--------------------+      +-------------------+
| Fetch / Prefixes | ---> | Decode ModRM / SIB | ---> | Execute / Faults  |
+------------------+      +--------------------+      +-------------------+
          |                         |                           |
          v                         v                           v
+------------------+      +--------------------+      +-------------------+
| Segment caches   | ---> | Paging + TLB       | ---> | Timing / Devices  |
+------------------+      +--------------------+      +-------------------+
```

### CPU, flags y control

- `EFLAGS` conserva ahora también el bit `ID`, útil para modelar la disponibilidad de `CPUID` en configuraciones compatibles.
- `CPU486` mantiene cachés explícitas de `GDTR`, `IDTR`, `LDTR` y `TR` mediante `DescriptorTableCache`.
- `TimingModel486` registra contadores para ciclos base, esperas de memoria, flushes de prefetch, stalls de pipeline y eventos de caché. Esta capa es deliberadamente opcional: las instrucciones siguen siendo funcionales aunque el modelo de microarquitectura esté incompleto.

### Segmentación y descriptores

`SegmentDescriptor` puede empaquetarse/desempaquetarse desde el formato binario x86 de 64 bits. Se modelan base, límite, granularidad de 4 KiB, DPL, presencia, bit de acceso, tamaño por defecto de 32 bits, segmentos expand-down, conforming code y permisos de lectura/escritura/ejecución.

```text
Selector
  bits 15..3: índice
  bit      2: TI (0=GDT, 1=LDT)
  bits  1..0: RPL

selector:offset -> descriptor (GDT/LDT) -> base + offset -> linear
```

### Paginación, TLB y fallos

La paginación mantiene una tabla de páginas simplificada pero ahora conserva bits `P`, `RW`, `US`, `A`, `D`, PWT y PCD. Los accesos que cruzan fronteras de página se dividen en varios accesos físicos, como necesita la ISA real para lecturas/escrituras desalineadas. El TLB cachea traducciones por página y se invalida con `INVLPG`, cambios de `CR3` y cambios relevantes de `CR0`.

```text
linear address
  directory index (futuro CR3/PDE) -> table index (PageMapping actual) -> frame + offset
                                      | P RW US A D PWT PCD |
```

### Instrucciones i486 ampliadas

Se añadieron rutas reales de ejecución para instrucciones que antes eran inexistentes o parciales:

- Sistema: `SGDT`, `SIDT`, `LGDT`, `LIDT`, `SMSW`, `LMSW`, `INVLPG`.
- i486: `CPUID` configurable de forma determinista, `CMPXCHG`, `XADD`, `BT`, `BTS`, `BTR`, `BTC`, `SHLD`, `SHRD`.
- Stack y control: `PUSHA`, `POPA`, `ENTER`, `RET imm16`, `LOOP`, `LOOPE`, `LOOPNE`, `JCXZ/JECXZ`.
- Operandos de 16 bits: el prefijo `0x66` ya afecta a `MOV r,imm`, `PUSH/POP`, `INC/DEC`, `CALL/JMP near`, `PUSHF/POPF` y retornos near.

### x87

La FPU conserva `Control Word`, `Status Word` y `Tag Word`. Además de constantes y aritmética de pila, soporta cargas y almacenamientos reales de `m32real`/`m64real`, `FSQRT` y comparaciones `FCOM` con actualización de C0/C2/C3 en el status word.

### Validación

La suite `i486_tests` cubre ahora:

- Accesos desalineados que cruzan páginas y actualizan bits A/D.
- Invalidación de TLB.
- `CPUID`, `XADD`, `CMPXCHG`, `BT/BTS`.
- `FLD m64real`, `FSQRT` y `FSTP m64real`.

### Próximos pasos para una réplica completa

1. Reemplazar `page_table` simplificada por recorrido real `CR3 -> PDE -> PTE`, preservando los atajos de test como modo de inyección.
2. Convertir la IDT simplificada en descriptores de interrupt, trap, task y call gates con validación CPL/RPL/DPL exacta.
3. Añadir PIC 8259 maestro/esclavo programable; conectar `IOBus::pending_irqs_` a IRR/ISR/IMR reales.
4. Completar PIT 8254 con modos 0..5, latch y BCD.
5. Añadir DMA 8237, RTC CMOS, UART 16550 y puerto paralelo usando `PortDevice`.
6. Sustituir latencias constantes por tablas por instrucción y penalizaciones de bus/prefetch/cache.
7. Completar todos los grupos x87, BCD, transcendentes, entorno (`FSTENV`, `FLDENV`, `FSAVE`, `FRSTOR`) y excepciones diferidas.
8. Ejecutar suites ISA externas y comparar contra manuales Intel 486 y trazas de emuladores consolidados.

## Expansión de toolchain y hardware

La pasada actual amplía también el flujo completo `486CC -> 486AS -> 486LD -> PC486Emulator`:

- `486CC` reconoce `for`, literales hexadecimales, operadores bitwise y bloques `asm { ... }` para insertar instrucciones i486 directamente en el ASM generado.
- `486AS` crece con `align`, `org`, datos con cadenas en `db`, memoria `byte/dword`, `[esp]`, más `Jcc`, instrucciones i486 (`cpuid`, `bswap`, `cmpxchg`, `xadd`, `bt/bts/btr/btc`) e instrucciones de I/O (`in`/`out`).
- `PC486Emulator` registra ahora interfaces de PIC 8259, DMA 8237, RTC/CMOS, serial COM1 y paralelo LPT1 además de PIT, VGA, SB16, teclado y storage.

```text
486CC source --asm{}--> 486AS text --relocs--> 486LD flat image
      |                                             |
      +---------- tests/compiler -------------------+--> PC486Emulator devices
```
