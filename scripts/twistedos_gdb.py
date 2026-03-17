import gdb


PROCESS_STATE_NAMES = {
    0: "RUNNING",
    1: "READY",
    2: "BLOCKED",
    3: "TERMINATED",
}


def _strip_typedefs(gdb_type):
    while True:
        stripped = gdb_type.strip_typedefs()
        if stripped == gdb_type:
            return stripped
        gdb_type = stripped


def _dereference(value):
    current = value
    current_type = _strip_typedefs(current.type)

    while current_type.code == gdb.TYPE_CODE_REF:
        current = current.referenced_value()
        current_type = _strip_typedefs(current.type)

    if current_type.code == gdb.TYPE_CODE_PTR:
        if int(current) == 0:
            raise gdb.GdbError("encountered null pointer")
        current = current.dereference()

    return current


def _field(value, name):
    obj = _dereference(value)

    try:
        return obj[name]
    except Exception:
        pass

    for field in _strip_typedefs(obj.type).fields():
        if field.name == name:
            return obj[field]

    raise gdb.GdbError("field '{}' not found in type '{}'".format(name, obj.type))


def _pointer_string(value):
    return "0x{:x}".format(int(value))


def _eval_or_none(expr):
    try:
        return gdb.parse_and_eval(expr)
    except gdb.error:
        return None


def _active_dispatcher():
    dispatcher = _eval_or_none("Dispatcher::ActiveDispatcher")
    if dispatcher is None:
        raise gdb.GdbError("could not resolve Dispatcher::ActiveDispatcher")
    if int(dispatcher) == 0:
        raise gdb.GdbError("Dispatcher::ActiveDispatcher is null")
    return dispatcher


def _default_process_manager():
    dispatcher = _active_dispatcher()
    logic = _field(dispatcher, "Logic")
    process_manager = _field(logic, "PM")
    if int(process_manager) == 0:
        raise gdb.GdbError("LogicLayer::PM is null")
    return process_manager


def _default_scheduler():
    dispatcher = _active_dispatcher()
    logic = _field(dispatcher, "Logic")
    scheduler = _field(logic, "Sched")
    if int(scheduler) == 0:
        raise gdb.GdbError("LogicLayer::Sched is null")
    return scheduler


def _default_sync_manager():
    dispatcher = _active_dispatcher()
    logic = _field(dispatcher, "Logic")
    sync_manager = _field(logic, "Sync")
    if int(sync_manager) == 0:
        raise gdb.GdbError("LogicLayer::Sync is null")
    return sync_manager


def _resolve_process_by_id(process_manager, process_id):
    manager_obj = _dereference(process_manager)
    processes = _field(manager_obj, "Processes")
    process_count = processes.type.range()[1] + 1

    for index in range(process_count):
        process = processes[index]
        if int(_field(process, "Id")) == process_id:
            return process

    raise gdb.GdbError("process with id {} not found".format(process_id))


def _as_int(value):
    return int(_dereference(value)) if _strip_typedefs(value.type).code == gdb.TYPE_CODE_REF else int(value)


class ArxCommand(gdb.Command):
    def __init__(self, name):
        super().__init__(name, gdb.COMMAND_USER)

    def _resolve_target(self, arg, default_resolver):
        arg = arg.strip()
        if arg:
            return gdb.parse_and_eval(arg)
        return default_resolver()


class ArxProcessesCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-processes")

    def invoke(self, arg, from_tty):
        process_manager = self._resolve_target(arg, _default_process_manager)
        manager_obj = _dereference(process_manager)
        processes = _field(manager_obj, "Processes")
        current_process_id = int(_field(manager_obj, "CurrentProcessId"))

        gdb.write("ProcessManager {} current={}\n".format(_pointer_string(process_manager), current_process_id))

        process_count = processes.type.range()[1] + 1
        for index in range(process_count):
            process = processes[index]
            process_id = int(_field(process, "Id"))
            status = int(_field(process, "Status"))
            state = _field(process, "State")
            stack_pointer = _field(process, "StackPointer")
            marker = "*" if process_id == current_process_id else " "

            gdb.write(
                "{} id={} status={} rip=0x{:x} rsp=0x{:x} stack={}\n".format(
                    marker,
                    process_id,
                    PROCESS_STATE_NAMES.get(status, str(status)),
                    int(_field(state, "rip")),
                    int(_field(state, "rsp")),
                    _pointer_string(stack_pointer),
                )
            )


class ArxReadyQueueCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-ready-queue")

    def invoke(self, arg, from_tty):
        scheduler = self._resolve_target(arg, _default_scheduler)
        scheduler_obj = _dereference(scheduler)
        ready_queue = _field(scheduler_obj, "ReadyQueue")
        current_process = _field(scheduler_obj, "CurrentProcess")
        head = _field(ready_queue, "HeadNode")

        gdb.write(
            "ReadyQueue {} current={}\n".format(
                _pointer_string(scheduler),
                _pointer_string(current_process),
            )
        )

        if int(head) == 0:
            gdb.write("  <empty>\n")
            return

        index = 0
        node = head
        while int(node) != 0:
            node_obj = _dereference(node)
            marker = "*" if int(node) == int(current_process) else " "
            gdb.write(
                "{} [{}] tag={} pid={} next={}\n".format(
                    marker,
                    index,
                    _pointer_string(node),
                    int(_field(node_obj, "Id")),
                    _pointer_string(_field(node_obj, "NextProcess")),
                )
            )
            node = _field(node_obj, "NextProcess")
            index += 1


class ArxSleepQueueCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-sleep-queue")

    def invoke(self, arg, from_tty):
        sync_manager = self._resolve_target(arg, _default_sync_manager)
        sync_obj = _dereference(sync_manager)
        sleep_queue = _field(sync_obj, "SleepQueue")
        head = _field(sleep_queue, "HeadNode")

        gdb.write("SleepQueue {}\n".format(_pointer_string(sync_manager)))

        if int(head) == 0:
            gdb.write("  <empty>\n")
            return

        index = 0
        node = head
        while int(node) != 0:
            node_obj = _dereference(node)
            gdb.write(
                "  [{}] tag={} pid={} wait_ticks={} next={}\n".format(
                    index,
                    _pointer_string(node),
                    int(_field(node_obj, "Id")),
                    int(_field(node_obj, "WaitTicksRemaining")),
                    _pointer_string(_field(node_obj, "Next")),
                )
            )
            node = _field(node_obj, "Next")
            index += 1


class ArxPhysicalMemoryCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-physical-memory")

    def invoke(self, arg, from_tty):
        try:
            # Get the active dispatcher and access the resource layer PMM
            dispatcher = _active_dispatcher()
            resource = _field(dispatcher, "Resource")
            pmm = _field(resource, "PMM")
            
            if int(pmm) == 0:
                raise gdb.GdbError("PhysicalMemoryManager is null")
            
            pmm_obj = _dereference(pmm)
            memory_descriptor = _field(pmm_obj, "MemoryDescriptorInfo")
            memory_descriptor_obj = _dereference(memory_descriptor)
            
            phys_addr_start = int(_field(memory_descriptor_obj, "PhysicalAddressStart"))
            total_pages = int(_field(memory_descriptor_obj, "TotalNumberOfPages"))
            free_pages = int(_field(memory_descriptor_obj, "NumberOfFreePages"))
            bitmap_ptr = _field(memory_descriptor_obj, "BitMap")
            
            if int(bitmap_ptr) == 0:
                raise gdb.GdbError("Memory descriptor bitmap is null")
            
            # Calculate bitmap size
            bitmap_bytes = (total_pages + 7) // 8
            
            # Read bitmap from memory
            try:
                bitmap_data = gdb.selected_inferior().read_memory(int(bitmap_ptr), bitmap_bytes)
            except Exception as e:
                raise gdb.GdbError("Failed to read bitmap: {}".format(e))
            
            # Convert bytes to integer array for easier bit manipulation
            bitmap = bytearray(bitmap_data)
            
            gdb.write("PhysicalMemoryManager Memory Bitmap\n")
            gdb.write("===================================\n")
            gdb.write("Physical Address Start: 0x{:x}\n".format(phys_addr_start))
            gdb.write("Total Pages: {} ({} MB)\n".format(total_pages, (total_pages * 4096) // (1024 * 1024)))
            gdb.write("Free Pages: {} ({} MB)\n".format(free_pages, (free_pages * 4096) // (1024 * 1024)))
            gdb.write("Allocated Pages: {} ({} MB)\n".format(total_pages - free_pages, ((total_pages - free_pages) * 4096) // (1024 * 1024)))
            gdb.write("Utilization: {:.1f}%\n".format((100.0 * (total_pages - free_pages)) / total_pages if total_pages > 0 else 0))
            gdb.write("\nAllocated Ranges:\n")
            gdb.write("-----------------\n")
            
            # Parse allocated ranges
            in_allocated = False
            allocated_start = 0
            allocated_count = 0
            
            for page_idx in range(total_pages):
                byte_idx = page_idx // 8
                bit_idx = page_idx % 8
                is_allocated = (bitmap[byte_idx] & (1 << bit_idx)) != 0
                
                if is_allocated:
                    if not in_allocated:
                        allocated_start = page_idx
                        allocated_count = 1
                        in_allocated = True
                    else:
                        allocated_count += 1
                else:
                    if in_allocated:
                        start_addr = phys_addr_start + (allocated_start * 4096)
                        end_addr = start_addr + (allocated_count * 4096) - 1
                        size_mb = (allocated_count * 4096) // (1024 * 1024)
                        gdb.write("  0x{:x} - 0x{:x} ({} pages, {} MB)\n".format(start_addr, end_addr, allocated_count, size_mb))
                        in_allocated = False
            
            # Handle case where bitmap ends while in allocated range
            if in_allocated:
                start_addr = phys_addr_start + (allocated_start * 4096)
                end_addr = start_addr + (allocated_count * 4096) - 1
                size_mb = (allocated_count * 4096) // (1024 * 1024)
                gdb.write("  0x{:x} - 0x{:x} ({} pages, {} MB)\n".format(start_addr, end_addr, allocated_count, size_mb))
            
            gdb.write("\nFree Ranges:\n")
            gdb.write("------------\n")
            
            # Parse free ranges
            in_free = False
            free_start = 0
            free_count = 0
            
            for page_idx in range(total_pages):
                byte_idx = page_idx // 8
                bit_idx = page_idx % 8
                is_allocated = (bitmap[byte_idx] & (1 << bit_idx)) != 0
                
                if not is_allocated:
                    if not in_free:
                        free_start = page_idx
                        free_count = 1
                        in_free = True
                    else:
                        free_count += 1
                else:
                    if in_free:
                        start_addr = phys_addr_start + (free_start * 4096)
                        end_addr = start_addr + (free_count * 4096) - 1
                        size_mb = (free_count * 4096) // (1024 * 1024)
                        gdb.write("  0x{:x} - 0x{:x} ({} pages, {} MB)\n".format(start_addr, end_addr, free_count, size_mb))
                        in_free = False
            
            # Handle case where bitmap ends while in free range
            if in_free:
                start_addr = phys_addr_start + (free_start * 4096)
                end_addr = start_addr + (free_count * 4096) - 1
                size_mb = (free_count * 4096) // (1024 * 1024)
                gdb.write("  0x{:x} - 0x{:x} ({} pages, {} MB)\n".format(start_addr, end_addr, free_count, size_mb))
        
        except gdb.GdbError as e:
            gdb.write("Error: {}\n".format(e))
        except Exception as e:
            gdb.write("Error reading memory bitmap: {}\n".format(e))


class ArxKernelHelpCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-help")

    def invoke(self, arg, from_tty):
        gdb.write("TwistedOS GDB commands:\n")
        gdb.write("  twistedos-processes [process_manager_expr]\n")
        gdb.write("  twistedos-ready-queue [scheduler_expr]\n")
        gdb.write("  twistedos-sleep-queue [sync_manager_expr]\n")
        gdb.write("  twistedos-address-space <process_id> [process_manager_expr]\n")
        gdb.write("  twistedos-physical-memory\n")
        gdb.write("If no expression is provided, commands resolve objects from Dispatcher::ActiveDispatcher.\n")


class ArxAddressSpaceCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-address-space")

    def invoke(self, arg, from_tty):
        args = [part for part in arg.split() if part]
        if len(args) == 0:
            raise gdb.GdbError("usage: twistedos-address-space <process_id> [process_manager_expr]")

        process_id = _as_int(gdb.parse_and_eval(args[0]))
        process_manager_expr = " ".join(args[1:])
        process_manager = self._resolve_target(process_manager_expr, _default_process_manager)

        process = _resolve_process_by_id(process_manager, process_id)
        address_space = _field(process, "AddressSpace")

        gdb.write(
            "AddressSpace pid={} process_manager={} process_level={}\n".format(
                process_id,
                _pointer_string(process_manager),
                int(_field(process, "Level")),
            )
        )

        if int(address_space) == 0:
            gdb.write("  <no address space>\n")
            return

        address_space_obj = _dereference(address_space)
        dynamic_type = _strip_typedefs(address_space_obj.dynamic_type)
        type_name = dynamic_type.name if dynamic_type.name else str(dynamic_type)

        gdb.write("  type={} ptr={}\n".format(type_name, _pointer_string(address_space)))

        code_pa = int(_field(address_space_obj, "CodePhysicalAddress"))
        code_size = int(_field(address_space_obj, "CodeSize"))
        code_va = int(_field(address_space_obj, "CodeVirtualAddressStart"))

        heap_pa = int(_field(address_space_obj, "HeapPhysicalAddress"))
        heap_size = int(_field(address_space_obj, "HeapSize"))
        heap_va = int(_field(address_space_obj, "HeapVirtualAddressStart"))

        stack_pa = int(_field(address_space_obj, "StackPhysicalAddress"))
        stack_size = int(_field(address_space_obj, "StackSize"))
        stack_va = int(_field(address_space_obj, "StackVirtualAddressStart"))

        gdb.write("  code : pa=0x{:x} va=0x{:x} size=0x{:x}\n".format(code_pa, code_va, code_size))
        gdb.write("  heap : pa=0x{:x} va=0x{:x} size=0x{:x}\n".format(heap_pa, heap_va, heap_size))
        gdb.write("  stack: pa=0x{:x} va=0x{:x} size=0x{:x}\n".format(stack_pa, stack_va, stack_size))

        if "VirtualAddressSpaceELF" not in type_name:
            return

        address_space_elf = _dereference(gdb.parse_and_eval("(VirtualAddressSpaceELF*)({})".format(int(address_space))))
        memory_region_count = int(_field(address_space_elf, "MemoryRegionCount"))
        memory_regions = _field(address_space_elf, "MemoryRegions")

        gdb.write("  elf header:\n")
        try:
            header = _dereference(gdb.parse_and_eval("*(ELFHeader*)({})".format(code_pa)))
            magic = [_as_int(header["Magic"][i]) for i in range(4)]
            gdb.write(
                "    magic={:02x} {:02x} {:02x} {:02x} entry=0x{:x} phoff=0x{:x} phnum={} machine=0x{:x}\n".format(
                    magic[0],
                    magic[1],
                    magic[2],
                    magic[3],
                    int(header["Entry"]),
                    int(header["ProgramHeaderOffset"]),
                    int(header["ProgramHeaderEntryCount"]),
                    int(header["Machine"]),
                )
            )
        except Exception as error:
            gdb.write("    <unavailable: {}>\n".format(error))

        gdb.write("  elf memory regions (count={}):\n".format(memory_region_count))
        if memory_region_count == 0:
            gdb.write("    <none>\n")
            return

        for index in range(memory_region_count):
            region = memory_regions[index]
            gdb.write(
                "    [{}] pa=0x{:x} va=0x{:x} size=0x{:x} writable={}\n".format(
                    index,
                    int(_field(region, "PhysicalAddress")),
                    int(_field(region, "VirtualAddress")),
                    int(_field(region, "Size")),
                    "yes" if int(_field(region, "Writable")) else "no",
                )
            )


ArxProcessesCommand()
ArxReadyQueueCommand()
ArxSleepQueueCommand()
ArxAddressSpaceCommand()
ArxPhysicalMemoryCommand()
ArxKernelHelpCommand()

gdb.write("Loaded TwistedOS GDB helpers. Run 'twistedos-help' for commands.\n")