#include "ResourceLayer.hpp"

ResourceLayer::ResourceLayer()
	: PMM(nullptr), VMM(nullptr), Console(nullptr), KernelHeapVirtualAddrStart(0), KernelHeapVirtualAddrEnd(0)
{
}

void ResourceLayer::Initialize(PhysicalMemoryManager* PMM, VirtualMemoryManager* VMM, FrameBufferConsole* Console,
							   uint64_t KernelHeapVirtualAddrStart, uint64_t KernelHeapVirtualAddrEnd)
{
	this->PMM                        = PMM;
	this->VMM                        = VMM;
	this->Console                    = Console;
	this->KernelHeapVirtualAddrStart = KernelHeapVirtualAddrStart;
	this->KernelHeapVirtualAddrEnd   = KernelHeapVirtualAddrEnd;
}

PhysicalMemoryManager* ResourceLayer::GetPMM() const
{
	return PMM;
}

VirtualMemoryManager* ResourceLayer::GetVMM() const
{
	return VMM;
}

FrameBufferConsole* ResourceLayer::GetConsole() const
{
	return Console;
}

uint64_t ResourceLayer::GetKernelHeapVirtualAddrStart() const
{
	return KernelHeapVirtualAddrStart;
}

uint64_t ResourceLayer::GetKernelHeapVirtualAddrEnd() const
{
	return KernelHeapVirtualAddrEnd;
}