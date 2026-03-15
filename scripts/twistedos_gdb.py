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


class ArxKernelHelpCommand(ArxCommand):
    def __init__(self):
        super().__init__("twistedos-help")

    def invoke(self, arg, from_tty):
        gdb.write("TwistedOS GDB commands:\n")
        gdb.write("  twistedos-processes [process_manager_expr]\n")
        gdb.write("  twistedos-ready-queue [scheduler_expr]\n")
        gdb.write("  twistedos-sleep-queue [sync_manager_expr]\n")
        gdb.write("If no expression is provided, commands resolve objects from Dispatcher::ActiveDispatcher.\n")


ArxProcessesCommand()
ArxReadyQueueCommand()
ArxSleepQueueCommand()
ArxKernelHelpCommand()

gdb.write("Loaded TwistedOS GDB helpers. Run 'twistedos-help' for commands.\n")