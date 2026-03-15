#pragma once

#include <stdint.h>

class RamFileSystemManager
{
private:
    uint64_t InitramfsAddress;
    uint64_t InitramfsSize;

public:
    RamFileSystemManager(uint64_t InitramfsAddress, uint64_t InitramfsSize);

    uint64_t GetInitramfsAddress() const;
    uint64_t GetInitramfsSize() const;
};