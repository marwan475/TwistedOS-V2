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
    VirtualAddressSpace* MapELF(uint64_t CodeAddr, uint64_t CodeSize, const ELFHeader& Header);
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
    void               InitializeProcessManager();
    void               InitializeScheduler();
    void               InitializeSynchronizationManager();
    void               InitializeELFManager();
    void               InitializeVirtualFileSystem();
    uint8_t            CreateNullProcess();
    uint8_t            CreateKernelProcess(void (*EntryPoint)());
    uint8_t            CreateUserProcess(uint64_t CodeAddr, uint64_t CodeSize);
    uint8_t            CreateUserProcessFromVFS(const char* FilePath);
    bool               RunProcess(uint8_t Id);
    void               KillProcess(uint8_t Id);
    void               SleepProcess(uint8_t Id, uint64_t WaitTicks);
    void               WakeProcess(uint8_t Id);
    void               BlockProcess(uint8_t Id);
    void               UnblockProcess(uint8_t Id);
    void               CaptureCurrentInterruptState(const Registers* Regs);
    void               Tick();
    void               Schedule();
    bool               isScheduling();
    void               EnableScheduling();
    void               DisableScheduling();
};