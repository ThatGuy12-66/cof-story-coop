"""Briefly sample x86 game thread registers; always restore suspension count."""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import time

K = c.WinDLL("kernel32", use_last_error=True)
INVALID = c.c_void_p(-1).value


class ThreadEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("usage", w.DWORD), ("tid", w.DWORD),
                ("pid", w.DWORD), ("base_priority", w.LONG), ("delta_priority", w.LONG), ("flags", w.DWORD)]


class ModuleEntry(c.Structure):
    _fields_ = [("size", w.DWORD), ("id", w.DWORD), ("pid", w.DWORD), ("global_usage", w.DWORD),
                ("usage", w.DWORD), ("base", c.c_void_p), ("bytes", w.DWORD), ("handle", w.HMODULE),
                ("name", w.WCHAR * 256), ("path", w.WCHAR * 260)]


def api(name, restype, *args):
    fn = getattr(K, name); fn.restype = restype; fn.argtypes = list(args)
    return fn


snapshot = api("CreateToolhelp32Snapshot", w.HANDLE, w.DWORD, w.DWORD)
close = api("CloseHandle", w.BOOL, w.HANDLE)
first_thread = api("Thread32First", w.BOOL, w.HANDLE, c.POINTER(ThreadEntry))
next_thread = api("Thread32Next", w.BOOL, w.HANDLE, c.POINTER(ThreadEntry))
first_module = api("Module32FirstW", w.BOOL, w.HANDLE, c.POINTER(ModuleEntry))
next_module = api("Module32NextW", w.BOOL, w.HANDLE, c.POINTER(ModuleEntry))
open_thread = api("OpenThread", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
open_process = api("OpenProcess", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
suspend = api("Wow64SuspendThread", w.DWORD, w.HANDLE)
resume = api("ResumeThread", w.DWORD, w.HANDLE)
get_context = api("Wow64GetThreadContext", w.BOOL, w.HANDLE, c.c_void_p)
read_memory = api("ReadProcessMemory", w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t))


def entries(pid):
    handle = snapshot(4, 0)
    if handle == INVALID:
        raise c.WinError(c.get_last_error())
    try:
        item = ThreadEntry(); item.size = c.sizeof(item)
        result = first_thread(handle, c.byref(item))
        while result:
            if item.pid == pid:
                yield item.tid
            result = next_thread(handle, c.byref(item))
    finally:
        close(handle)


def modules(pid):
    result = []
    handle = snapshot(8 | 16, pid)
    if handle == INVALID:
        raise c.WinError(c.get_last_error())
    try:
        item = ModuleEntry(); item.size = c.sizeof(item)
        found = first_module(handle, c.byref(item))
        while found:
            result.append({"name": item.name, "base": item.base, "size": item.bytes})
            found = next_module(handle, c.byref(item))
    finally:
        close(handle)
    return result


def sample(pid):
    mods = modules(pid)
    def locate(address):
        for module in mods:
            if module["base"] <= address < module["base"] + module["size"]:
                return f"{module['name']}+0x{address - module['base']:x}"
        return f"0x{address:08x}"
    process = open_process(0x410, False, pid)
    if not process:
        raise c.WinError(c.get_last_error())
    def memory(address, size):
        buffer = c.create_string_buffer(size); actual = c.c_size_t()
        if read_memory(process, address, buffer, size, c.byref(actual)):
            return buffer.raw[:actual.value]
        return b""
    records = []
    try:
        for tid in list(entries(pid)):
            handle = open_thread(0x004A, False, tid)
            if not handle:
                continue
            suspended = False
            try:
                context = c.create_string_buffer(716)
                struct.pack_into("<I", context, 0, 0x10003)
                count = suspend(handle)
                if count == 0xFFFFFFFF:
                    continue
                suspended = True
                if not get_context(handle, context):
                    records.append({"tid": tid, "error": c.get_last_error()})
                    continue
                raw = context.raw
                regs = dict(zip(["edi", "esi", "ebx", "edx", "ecx", "eax", "ebp", "eip", "cs", "eflags", "esp", "ss"], struct.unpack_from("<12I", raw, 156)))
                stack = memory(regs["esp"], 256)
                frame = regs["ebp"]
                chain = []
                for _ in range(12):
                    data = memory(frame, 8)
                    if len(data) != 8:
                        break
                    following, ret = struct.unpack("<II", data)
                    chain.append(locate(ret))
                    if following <= frame or following - frame > 0x100000:
                        break
                    frame = following
                records.append({"tid": tid, "ip": locate(regs["eip"]), "registers": {key: f"0x{value:08x}" for key, value in regs.items()},
                                "frame_chain": chain, "stack_words": [locate(value) for (value,) in struct.iter_unpack("<I", stack[:len(stack)//4*4])]})
            finally:
                if suspended:
                    if resume(handle) == 0xFFFFFFFF:
                        raise RuntimeError(f"CRITICAL: thread {tid} could not be resumed; Windows error {c.get_last_error()}")
                close(handle)
    finally:
        close(process)
    return {"utc": datetime.now(timezone.utc).isoformat(), "pid": pid, "modules": mods, "threads": records}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("pid", type=int)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    output = []
    for index in range(3):
        output.append(sample(args.pid))
        if index < 2:
            time.sleep(0.4)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(output, indent=2))
    for record in output:
        print(record["utc"], [(thread["tid"], thread.get("ip", thread.get("error"))) for thread in record["threads"]])
