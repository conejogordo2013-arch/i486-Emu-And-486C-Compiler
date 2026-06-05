# Arquitectura completa del compilador 486CC

486CC es el compilador freestanding para el lenguaje C486. Su objetivo es producir código Intel i486 sin runtime pesado y con una ruta transparente:

```text
.c486 -> preprocesador -> lexer/parser -> AST -> tipos -> IR -> optimizador -> ASM 486 -> OBJ -> BIN -> emulador i486
```

## 1. Frontend

El frontend se divide en tres capas:

1. **Preprocesador:** resuelve `#include`, `#define`, macros con parámetros, `#if`, `#ifdef`, `#ifndef`, `#else`, `#elif` y `#endif`. Produce un stream de texto/tokens ya expandido para el lexer.
2. **Lexer:** convierte caracteres en tokens: identificadores, literales, keywords, operadores, delimitadores y directivas.
3. **Parser:** construye el AST con descenso recursivo y precedencia de operadores. La implementación inicial compila funciones, variables locales, expresiones, llamadas, `if`, `while` y `return`; la arquitectura reserva nodos para `for`, `switch`, structs, arrays y punteros.

## 2. Lenguaje C486

C486 es un subconjunto estilo C orientado a i486 freestanding.

### Tipos

- Primitivos: `void`, `bool`, `char`, `int`, `float`, `double`.
- Compuestos: punteros `T*`, arrays `T[N]`, `struct Name { ... }`.
- Tamaños i486: `bool/char=1`, `int/float/pointer=4`, `double=8` con alineación máxima 4 para ABI simple.

### Sentencias y expresiones

Incluye variables, funciones, structs, punteros, arrays, llamadas, `if/else`, `while`, `for`, `switch`, `return`, asignación, operadores aritméticos, comparación, lógicos, bitwise, direccionamiento `&`, indirección `*`, acceso `.` y `->`.

### Gramática base

```ebnf
translation_unit = { function | struct_decl } ;
function         = type identifier "(" [ parameters ] ")" compound ;
parameters       = parameter { "," parameter } ;
parameter        = type identifier ;
compound         = "{" { statement } "}" ;
statement        = var_decl ";" | return_stmt | if_stmt | while_stmt | for_stmt | switch_stmt | expr ";" | compound ;
var_decl         = type identifier [ "=" expr ] ;
return_stmt      = "return" expr ";" ;
if_stmt          = "if" "(" expr ")" statement [ "else" statement ] ;
while_stmt       = "while" "(" expr ")" statement ;
expr             = assignment ;
assignment       = equality [ "=" assignment ] ;
equality         = relational { ("==" | "!=") relational } ;
relational       = additive { ("<" | "<=" | ">" | ">=") additive } ;
additive         = multiplicative { ("+" | "-") multiplicative } ;
multiplicative   = unary { ("*" | "/" | "%") unary } ;
unary            = ("-" | "!" | "*" | "&") unary | primary ;
primary          = integer | char | identifier | call | "(" expr ")" ;
```

### Restricciones i486

- No requiere OS ni libc externa.
- ABI de 32 bits, little-endian, stack descendente.
- Sin instrucciones posteriores a i486.
- `double` se baja a x87 cuando se active el backend FP completo.
- El runtime es opcional salvo entry/heap cuando se genera binario freestanding.

## 3. AST

El AST representa el programa con nodos fuertemente tipados:

- Expresiones: `IntegerExpr`, `VariableExpr`, `BinaryExpr`, `UnaryExpr`, `CallExpr`.
- Sentencias: `VarDecl`, `ReturnStmt`, `ExprStmt`, `CompoundStmt`, `IfStmt`, `WhileStmt`, `ForStmt`.
- Top-level: `FunctionDecl`, `StructDecl`, `TranslationUnit`.

El parser crea nodos con `unique_ptr` para expresar propiedad única y permitir árboles grandes sin copias accidentales.

## 4. Sistema de tipos

La representación interna `Type` contiene:

- `TypeKind`: primitivo, puntero, array o struct.
- `name`: nombre legible/ABI.
- `element`: tipo apuntado o elemento de array.
- `array_count`.
- `size` y `align`.

Reglas de promoción:

1. `char` y `bool` promocionan a `int` en ALU.
2. `float` promociona a `double` si se mezcla con `double`.
3. Puntero + entero escala por tamaño del elemento.
4. Conversiones explícitas permiten truncar; implícitas evitan pérdida salvo promoción.
5. Structs se alinean a máximo 4 bytes en i486.

## 5. IR intermedio

El IR es de bajo nivel e independiente del lenguaje. Opera con temporales, nombres locales y etiquetas.

Instrucciones:

- `Mov dst, src`
- `Load dst, addr`, `Store addr, src`
- `Add/Sub/Mul/Div dst, a, b`
- `Cmp dst, a, b`
- `Jmp label`, `Jcc cond, a, b, label`
- `Call dst, callee`
- `Ret value`
- `Label label`

Ejemplo:

```c
int main() { int x = 1 + 2; return x; }
```

IR optimizado:

```text
mov x, 3
ret x
```

## 6. Optimizador

Pases iniciales:

- Constant folding: `1+2` pasa a `3`.
- Constant propagation: valores constantes locales pueden sustituirse.
- Dead code elimination: elimina instrucciones después de `ret` hasta la próxima etiqueta.
- Simplificación de expresiones: identidades aritméticas como `x+0`, `x*1`.
- Optimización de saltos: elimina saltos a la siguiente etiqueta.
- Registros básica: temporales calientes se asignan a `EAX`, `EBX`, `ECX`, `EDX`, `ESI`, `EDI` antes de usar spills.

## 7. Backend Intel 486

El backend genera ensamblador Intel 32-bit:

- Registros scratch: `EAX`, `ECX`, `EDX`.
- Preservados: `EBX`, `ESI`, `EDI`, `EBP`.
- Stack frame:

```asm
push ebp
mov  ebp, esp
sub  esp, locals_size
...
mov  esp, ebp
pop  ebp
ret
```

- Retorno: enteros/punteros/bool/char en `EAX`; `double` en `ST0` cuando se habilite x87 ABI completo.
- Parámetros: por stack, derecha a izquierda, caller cleanup por defecto estilo cdecl simple.

Ejemplo IR -> ASM:

```text
add %t0, a, b
ret %t0
```

```asm
mov eax, [ebp+8]
add eax, [ebp+12]
mov [ebp-4], eax
mov eax, [ebp-4]
mov esp, ebp
pop ebp
ret
```

## 8. Convención de llamadas 486CC

- Parámetros en stack de derecha a izquierda.
- Valor de retorno en `EAX` para 1/4 bytes; `EDX:EAX` reservado para 64 bits; `ST0` para floating x87.
- Caller-saved: `EAX`, `ECX`, `EDX`.
- Callee-saved: `EBX`, `ESI`, `EDI`, `EBP`.
- Stack alineado a 4 bytes.
- Llamadas anidadas funcionan apilando parámetros y limpiando después de `call`.

## 9. Librería estándar mínima

Freestanding, sin OS obligatorio:

- `stdint.h`: `u8/u16/u32/i8/i16/i32`.
- `stdbool.h`: `bool`, `true`, `false` si se compila en modo compatibilidad.
- `string.h`: `memcpy`, `memset`, `strlen`, `strcmp`.
- `stdlib.h`: bump allocator `malloc/free` simple.
- `stdio.h`: salida VGA/debug `putc`, `puts`, `printf` mínimo.

## 10. Runtime mínimo

- Entry `_start` inicializa `ESP`, limpia `.bss`, inicializa heap y llama `main`.
- Heap bump-pointer opcional en memoria freestanding.
- Sin hilos, señales, archivos ni syscalls obligatorias.
- Argumentos: `argc/argv` opcionales, inyectados por emulador o bootloader.

## 11. Toolchain completo

Componentes:

1. `486cc`: `.c486 -> .asm`.
2. `486as`: `.asm -> .obj` simple con secciones `.text/.data/.bss`, símbolos y relocaciones.
3. `486ld`: `.obj -> .bin` plano o imagen bootable.
4. Emulador i486: carga el binario en memoria y salta al entry.
5. Símbolos debug opcionales: mapa `address -> file:line:function`.

Formato objeto mínimo:

```text
magic C486OBJ
sections[]
symbols[]
relocations[]
```

Formato BIN mínimo: binario plano little-endian con entry configurado por linker script o default `_start`.
