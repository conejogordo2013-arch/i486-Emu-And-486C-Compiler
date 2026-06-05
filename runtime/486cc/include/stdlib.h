#pragma once
#include "stdint.h"
void heap_init(void* start, u32 bytes);
void* malloc(u32 bytes);
void free(void* ptr);
