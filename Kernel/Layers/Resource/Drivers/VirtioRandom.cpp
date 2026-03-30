/**
 * File: VirtioRandom.cpp
 * Author: Marwan Mostafa
 * Description: VirtIO RNG (virtio-rng-pci legacy I/O) driver implementation.
 */

#include "VirtioRandom.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Layers/Logic/VirtualFileSystem.hpp>
#include <Memory/PhysicalMemoryManager.hpp>

namespace
{
uint32_t AlignUpTo(uint32_t Value, uint32_t Alignment)
{
    if (Alignment == 0)
    {
        return Value;
    }

    uint32_t Remainder = Value % Alignment;
    if (Remainder == 0)
    {
        return Value;
    }

    return Value + (Alignment - Remainder);
}
} // namespace

FileOperations VirtioRandom::RandomFileOperations = {
    &VirtioRandom::ReadFileOperation, &VirtioRandom::WriteFileOperation, &VirtioRandom::SeekFileOperation, &VirtioRandom::MemoryMapFileOperation, nullptr,
    &VirtioRandom::IoctlFileOperation,
};

VirtioRandom::VirtioRandom(uint16_t IoBase, PhysicalMemoryManager* PMM)
    : IoBase(IoBase), Initialized(false), QueueSize(0), QueueMemory(nullptr), QueueMemoryPhysical(0), LastUsedIndex(0), DataOffset(0), PMM(PMM)
{
}

VirtioRandom::~VirtioRandom()
{
    if (QueueMemory != nullptr && PMM != nullptr)
    {
        PMM->FreePagesFromDescriptor(QueueMemory, VIRTIO_QUEUE_MEMORY_PAGES);
        QueueMemory         = nullptr;
        QueueMemoryPhysical = 0;
    }
}

bool VirtioRandom::Initialize()
{
    if (PMM == nullptr || IoBase == 0)
    {
        return false;
    }

    X86OutB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_DEVICE_STATUS_OFFSET), 0);
    X86OutB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_DEVICE_STATUS_OFFSET), VIRTIO_STATUS_ACKNOWLEDGE);
    X86OutB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_DEVICE_STATUS_OFFSET), VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    X86OutL(static_cast<uint16_t>(IoBase + VIRTIO_PCI_GUEST_FEATURES_OFFSET), 0);

    if (!SetupQueue())
    {
        X86OutB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_DEVICE_STATUS_OFFSET), 0);
        return false;
    }

    X86OutB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_DEVICE_STATUS_OFFSET), VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    Initialized = true;
    return true;
}

bool VirtioRandom::SetupQueue()
{
    X86OutW(static_cast<uint16_t>(IoBase + VIRTIO_PCI_QUEUE_SELECT_OFFSET), static_cast<uint16_t>(VIRTIO_QUEUE_INDEX_RNG));

    uint16_t DeviceQueueSize = X86InW(static_cast<uint16_t>(IoBase + VIRTIO_PCI_QUEUE_SIZE_OFFSET));
    if (DeviceQueueSize == 0)
    {
        return false;
    }

    QueueSize = DeviceQueueSize;

    QueueMemory = PMM->AllocatePagesFromDescriptor(VIRTIO_QUEUE_MEMORY_PAGES);
    if (QueueMemory == nullptr)
    {
        return false;
    }

    QueueMemoryPhysical = reinterpret_cast<uint64_t>(QueueMemory);
    if ((QueueMemoryPhysical >> 12) > 0xFFFFFFFFULL)
    {
        PMM->FreePagesFromDescriptor(QueueMemory, VIRTIO_QUEUE_MEMORY_PAGES);
        QueueMemory         = nullptr;
        QueueMemoryPhysical = 0;
        return false;
    }

    kmemset(QueueMemory, 0, PAGE_SIZE * VIRTIO_QUEUE_MEMORY_PAGES);

    uint32_t DescriptorBytes = static_cast<uint32_t>(sizeof(VirtQueueDescriptor)) * static_cast<uint32_t>(QueueSize);
    uint32_t AvailableBytes  = static_cast<uint32_t>(sizeof(uint16_t) * 2 + sizeof(uint16_t) * QueueSize + sizeof(uint16_t));
    uint32_t UsedBytes       = static_cast<uint32_t>(sizeof(uint16_t) * 2 + sizeof(VirtQueueUsedElement) * QueueSize + sizeof(uint16_t));

    uint32_t AvailableOffset = DescriptorBytes;
    uint32_t UsedOffset      = AlignUpTo(AvailableOffset + AvailableBytes, VIRTIO_LEGACY_VRING_ALIGN);
    DataOffset               = AlignUpTo(UsedOffset + UsedBytes, 16);

    if (DataOffset + VIRTIO_MAX_RANDOM_CHUNK > (PAGE_SIZE * VIRTIO_QUEUE_MEMORY_PAGES))
    {
        PMM->FreePagesFromDescriptor(QueueMemory, VIRTIO_QUEUE_MEMORY_PAGES);
        QueueMemory         = nullptr;
        QueueMemoryPhysical = 0;
        return false;
    }

    uint32_t QueuePageFrameNumber = static_cast<uint32_t>(QueueMemoryPhysical >> 12);
    X86OutL(static_cast<uint16_t>(IoBase + VIRTIO_PCI_QUEUE_ADDRESS_OFFSET), QueuePageFrameNumber);

    LastUsedIndex = 0;
    return true;
}

bool VirtioRandom::IsInitialized() const
{
    return Initialized;
}

bool VirtioRandom::HandleInterrupt() const
{
    if (!Initialized || IoBase == 0)
    {
        return false;
    }

    uint8_t InterruptStatus = X86InB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_ISR_STATUS_OFFSET));
    return InterruptStatus != 0;
}

bool VirtioRandom::SubmitEntropyRequest(uint32_t RequestLength, uint8_t* DestinationBuffer)
{
    if (!Initialized || QueueMemory == nullptr || DestinationBuffer == nullptr || RequestLength == 0 || RequestLength > VIRTIO_MAX_RANDOM_CHUNK)
    {
        return false;
    }

    uint8_t* QueueBytes = reinterpret_cast<uint8_t*>(QueueMemory);

    VirtQueueDescriptor* DescriptorTable = reinterpret_cast<VirtQueueDescriptor*>(QueueBytes);
    VirtQueueAvailable*  AvailableRing   = reinterpret_cast<VirtQueueAvailable*>(QueueBytes + (sizeof(VirtQueueDescriptor) * QueueSize));

    uint32_t AvailableBytes = static_cast<uint32_t>(sizeof(uint16_t) * 2 + sizeof(uint16_t) * QueueSize + sizeof(uint16_t));
    uint32_t UsedOffset     = AlignUpTo(static_cast<uint32_t>(sizeof(VirtQueueDescriptor) * QueueSize) + AvailableBytes, VIRTIO_LEGACY_VRING_ALIGN);
    VirtQueueUsed* UsedRing = reinterpret_cast<VirtQueueUsed*>(QueueBytes + UsedOffset);

    uint8_t*  EntropyBuffer     = QueueBytes + DataOffset;
    uint64_t  EntropyBufferPhys = QueueMemoryPhysical + DataOffset;
    uint16_t  AvailableIndex    = AvailableRing->Index;
    uint16_t  DescriptorIndex   = 0;

    DescriptorTable[DescriptorIndex].Address = EntropyBufferPhys;
    DescriptorTable[DescriptorIndex].Length  = RequestLength;
    DescriptorTable[DescriptorIndex].Flags   = VIRTQ_DESC_F_WRITE;
    DescriptorTable[DescriptorIndex].Next    = 0;

    AvailableRing->Ring[AvailableIndex % QueueSize] = DescriptorIndex;
    __asm__ __volatile__("" ::: "memory");
    AvailableRing->Index = static_cast<uint16_t>(AvailableIndex + 1);

    X86OutW(static_cast<uint16_t>(IoBase + VIRTIO_PCI_QUEUE_NOTIFY_OFFSET), static_cast<uint16_t>(VIRTIO_QUEUE_INDEX_RNG));

    uint32_t SpinCount = 0;
    while (UsedRing->Index == LastUsedIndex)
    {
        if (++SpinCount > 10000000)
        {
            return false;
        }
    }

    VirtQueueUsedElement CompletedElement = UsedRing->Ring[LastUsedIndex % QueueSize];
    LastUsedIndex = static_cast<uint16_t>(LastUsedIndex + 1);

    volatile uint8_t InterruptStatus = X86InB(static_cast<uint16_t>(IoBase + VIRTIO_PCI_ISR_STATUS_OFFSET));
    (void) InterruptStatus;

    if (CompletedElement.Length == 0)
    {
        return false;
    }

    uint32_t BytesToCopy = (CompletedElement.Length < RequestLength) ? CompletedElement.Length : RequestLength;
    memcpy(DestinationBuffer, EntropyBuffer, BytesToCopy);
    return BytesToCopy == RequestLength;
}

bool VirtioRandom::GetRandomBytes(void* Buffer, uint64_t Count)
{
    if (!Initialized || (Buffer == nullptr && Count != 0))
    {
        return false;
    }

    if (Count == 0)
    {
        return true;
    }

    uint8_t* Destination  = reinterpret_cast<uint8_t*>(Buffer);
    uint64_t Remaining    = Count;
    uint64_t OutputOffset = 0;

    while (Remaining > 0)
    {
        uint32_t ChunkSize = (Remaining > VIRTIO_MAX_RANDOM_CHUNK) ? VIRTIO_MAX_RANDOM_CHUNK : static_cast<uint32_t>(Remaining);
        if (!SubmitEntropyRequest(ChunkSize, Destination + OutputOffset))
        {
            return false;
        }

        Remaining -= ChunkSize;
        OutputOffset += ChunkSize;
    }

    return true;
}

const char* VirtioRandom::GetDeviceFileName() const
{
    return "/dev/urandom";
}

FileOperations* VirtioRandom::GetFileOperations()
{
    return &RandomFileOperations;
}

int64_t VirtioRandom::Read(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || Buffer == nullptr)
    {
        return -14;
    }

    if (!GetRandomBytes(Buffer, Count))
    {
        return -5;
    }

    return static_cast<int64_t>(Count);
}

int64_t VirtioRandom::ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || OpenFile->Node->NodeData == nullptr)
    {
        return -14;
    }

    VirtioRandom* Driver = reinterpret_cast<VirtioRandom*>(OpenFile->Node->NodeData);
    return Driver->Read(OpenFile, Buffer, Count);
}

int64_t VirtioRandom::WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    (void) OpenFile;
    (void) Buffer;
    (void) Count;
    return -1;
}

int64_t VirtioRandom::SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    (void) OpenFile;
    (void) Offset;
    (void) Whence;
    return -29;
}

int64_t VirtioRandom::MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    (void) OpenFile;
    (void) Length;
    (void) Offset;
    (void) AddressSpace;
    (void) Address;
    return -38;
}

int64_t VirtioRandom::IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
    (void) OpenFile;
    (void) Request;
    (void) Argument;
    (void) Logic;
    (void) RunningProcess;
    return -25;
}
