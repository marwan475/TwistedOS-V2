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

char* stcpy(char* dest, const char* src)
{
    return strcpy(dest, src);
}
