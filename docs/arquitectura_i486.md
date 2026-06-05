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
