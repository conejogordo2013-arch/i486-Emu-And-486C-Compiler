#pragma once
#include "stdint.h486"

/* Intrinsics de bajo nivel para 486CC. Se usan dentro de bloques asm{} o
   como documentación de las instrucciones que el assembler/emulador soporta. */
#define i486_cli() asm { cli; }
#define i486_sti() asm { sti; }
#define i486_cld() asm { cld; }
#define i486_std() asm { std; }
#define i486_invd() asm { invd; }
#define i486_wbinvd() asm { wbinvd; }
#define i486_cpuid() asm { cpuid; }
#define i486_rep_movsd() asm { rep movsd; }
#define i486_rep_stosd() asm { rep stosd; }
