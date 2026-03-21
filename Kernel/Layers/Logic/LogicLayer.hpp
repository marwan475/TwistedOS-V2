/**
 * File: LogicLayer.hpp
 * Author: Marwan Mostafa
 * Description: Logic layer orchestration interface declarations.
 */

#pragma once

#include "ELFManager.hpp"
#include "ProcessManager.hpp"
#include "Scheduler.hpp"
#include "SynchronizationManager.hpp"
#include "VirtualFileSystem.hpp"

#include <Arch/x86.hpp>
#include <stdint.h>

extern uint64_t Ticks;

class ResourceLayer;

class LogicLayer
{
private:
    ResourceLayer*          Resource;
    ProcessManager*         PM;
    Scheduler*              Sched;
    SynchronizationManager* Sync;
    ELFManager*             ELF;
    VirtualFileSystem*      VFS;
    bool                    IsScheduling = false;

    VirtualAddressSpace* MapRawBinary(uint64_t CodeAddr, uint64_t CodeSize);
    VirtualAddressSpace* MapELF(uint64_t CodeAddr, uint64_t CodeSize, const ELFHeader& Header, const VirtualAddressSpaceELF* SourceRuntimeELFAddressSpace = nullptr);
    void                 CleanUpELF(VirtualAddressSpace* AddressSpace);
    void                 CleanUpRawBinary(VirtualAddressSpace* AddressSpace);

public:
    LogicLayer();
    ~LogicLayer();
    void Initialize(ResourceLayer* Resource);

    ResourceLayer*     GetResourceLayer() const;
    ELFManager*        GetELFManager() const;
    ProcessManager*    GetProcessManager() const;
    VirtualFileSystem* GetVirtualFileSystem() const;
    void*              kmalloc(uint64_t Size);
    void               kfree(void* Pointer);
    void               InitializeProcessManager();
    void               InitializeScheduler();
    void               InitializeSynchronizationManager();
    void               InitializeELFManager();
    void               InitializeVirtualFileSystem();
    bool               RegisterPartitionDevices();
    bool               InitializeExtendedFileSystem(const char* DevicePath, const char* MountLocation);
    uint8_t            CreateNullProcess();
    uint8_t            CreateKernelProcess(void (*EntryPoint)());
    uint8_t            CreateUserProcess(uint64_t CodeAddr, uint64_t CodeSize, const char* InitialArgv0 = nullptr);
    uint8_t            CreateUserProcessFromVFS(const char* FilePath);
    uint8_t            CreateInitProcess();
    uint8_t            ChangeProcessExecution(uint8_t Id, const char* FilePath, const char* const* Argv, uint64_t Argc, const char* const* Envp, uint64_t Envc);
    uint8_t            CopyProcess(uint8_t Id);
    bool               RunProcess(uint8_t Id);
    bool               CopyFromUserToKernel(const void* UserSource, void* KernelDestination, uint64_t Count);
    bool               CopyFromKernelToUser(const void* KernelSource, void* UserDestination, uint64_t Count);
    void               KillProcess(uint8_t Id, int32_t ExitStatus = 0);
    void               SleepProcess(uint8_t Id, uint64_t WaitTicks);
    void               WakeProcess(uint8_t Id);
    void               BlockProcessForTTYInput(uint8_t Id);
    void               NotifyTTYInputAvailable();
    void               BlockProcess(uint8_t Id);
    void               UnblockProcess(uint8_t Id);
    void               AddProcessToReadyQueue(uint8_t Id);
    void               CaptureCurrentInterruptState(const Registers* Regs);
    void               Tick();
    void               Schedule();
    bool               isScheduling();
    void               EnableScheduling();
    void               DisableScheduling();
};