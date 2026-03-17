/**
 * File: CommonUtils.hpp
 * Author: Marwan Mostafa
 * Description: Common low-level utility function declarations.
 */

#pragma once

#include <stddef.h>

void  kmemset(void* dest, int value, size_t count);
char* strcpy(char* dest, const char* src);
void  memcpy(void* dest, const void* src, size_t count);
