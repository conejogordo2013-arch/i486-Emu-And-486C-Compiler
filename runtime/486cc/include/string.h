#pragma once
#include "stdint.h"
void* memcpy(void* dst, const void* src, u32 n);
void* memset(void* dst, int value, u32 n);
u32 strlen(const char* s);
int strcmp(const char* a, const char* b);
