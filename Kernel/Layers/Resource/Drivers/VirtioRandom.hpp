/**
 * File: VirtioRandom.hpp
 * Author: Marwan Mostafa
 * Description: VirtIO RNG (virtio-rng-pci legacy I/O) driver declarations.
 */

#pragma once

#include <stdint.h>

class PhysicalMemoryManager;
struct File;
struct FileOperations;
class LogicLayer;
struct Process;

class VirtioRandom
{
private:
    struct VirtQueueDescriptor
    {
        uint64_t Address;
        uint32_t Length;
        uint16_t Flags;
        uint16_t Next;
    } __attribute__((packed));

    struct VirtQueueAvailable
    {
        uint16_t Flags;
        uint16_t Index;
        uint16_t Ring[1];
    } __attribute__((packed));

    struct VirtQueueUsedElement
    {
        uint32_t Id;
        uint32_t Length;
    } __attribute__((packed));

    struct VirtQueueUsed
    {
        uint16_t           Flags;
        uint16_t           Index;
        VirtQueueUsedElement Ring[1];
    } __attribute__((packed));

    static constexpr uint32_t VIRTIO_PCI_QUEUE_SELECT_OFFSET  = 0x0E;
    static constexpr uint32_t VIRTIO_PCI_QUEUE_NOTIFY_OFFSET  = 0x10;
    static constexpr uint32_t VIRTIO_PCI_DEVICE_STATUS_OFFSET = 0x12;
    static constexpr uint32_t VIRTIO_PCI_ISR_STATUS_OFFSET    = 0x13;
    static constexpr uint32_t VIRTIO_PCI_QUEUE_ADDRESS_OFFSET = 0x08;
    static constexpr uint32_t VIRTIO_PCI_QUEUE_SIZE_OFFSET    = 0x0C;
    static constexpr uint32_t VIRTIO_PCI_GUEST_FEATURES_OFFSET = 0x04;

    static constexpr uint8_t VIRTIO_STATUS_ACKNOWLEDGE = 0x01;
    static constexpr uint8_t VIRTIO_STATUS_DRIVER      = 0x02;
    static constexpr uint8_t VIRTIO_STATUS_DRIVER_OK   = 0x04;
    static constexpr uint8_t VIRTQ_DESC_F_WRITE        = 0x02;

    static constexpr uint32_t VIRTIO_QUEUE_INDEX_RNG       = 0;
    static constexpr uint32_t VIRTIO_LEGACY_VRING_ALIGN    = 4096;
    static constexpr uint32_t VIRTIO_QUEUE_MEMORY_PAGES    = 2;
    static constexpr uint32_t VIRTIO_MAX_RANDOM_CHUNK      = 256;

    uint16_t               IoBase;
    bool                   Initialized;
    uint16_t               QueueSize;
    void*                  QueueMemory;
    uint64_t               QueueMemoryPhysical;
    uint16_t               LastUsedIndex;
    uint32_t               DataOffset;
    PhysicalMemoryManager* PMM;
    static FileOperations  RandomFileOperations;

    bool SetupQueue();
    bool SubmitEntropyRequest(uint32_t RequestLength, uint8_t* DestinationBuffer);

public:
    VirtioRandom(uint16_t IoBase, PhysicalMemoryManager* PMM);
    ~VirtioRandom();

    bool Initialize();
    bool IsInitialized() const;
    bool HandleInterrupt() const;
    bool GetRandomBytes(void* Buffer, uint64_t Count);
    const char* GetDeviceFileName() const;
    FileOperations* GetFileOperations();

    int64_t Read(File* OpenFile, void* Buffer, uint64_t Count);
    static int64_t ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count);
    static int64_t WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count);
    static int64_t SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence);
    static int64_t MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, class VirtualAddressSpace* AddressSpace, uint64_t* Address);
    static int64_t IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess);
};
