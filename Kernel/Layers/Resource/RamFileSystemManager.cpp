#include "RamFileSystemManager.hpp"

RamFileSystemManager::RamFileSystemManager(uint64_t InitramfsAddress, uint64_t InitramfsSize)
    : InitramfsAddress(InitramfsAddress), InitramfsSize(InitramfsSize)
{
}

uint64_t RamFileSystemManager::GetInitramfsAddress() const
{
    return InitramfsAddress;
}

uint64_t RamFileSystemManager::GetInitramfsSize() const
{
    return InitramfsSize;
}