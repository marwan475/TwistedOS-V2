/**
 * File: CommonUtils.cpp
 * Author: Marwan Mostafa
 * Description: Common low-level utility function implementations.
 */

#include <CommonUtils.hpp>

void kmemset(void* dest, int value, size_t count)
{
    unsigned char* ptr  = (unsigned char*) dest;
    unsigned char  byte = (unsigned char) value;

    for (size_t i = 0; i < count; i++)
    {
        ptr[i] = byte;
    }
}

char* strcpy(char* dest, const char* src)
{
    char* d = dest;

    while ((*d++ = *src++))
    {
    }

    return dest;
}

void memcpy(void* dest, const void* src, size_t count)
{
    unsigned char*       d = (unsigned char*) dest;
    const unsigned char* s = (const unsigned char*) src;

    for (size_t i = 0; i < count; i++)
    {
        d[i] = s[i];
    }
}
