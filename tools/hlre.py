"""Read-only static helpers for the stock Cry of Fear hl.dll (SHA-256 0036B91C...0E63).

Needs capstone and pefile (not in the launcher's own requirements): run it from a
throwaway venv, e.g. `python -m venv <tmp>\\revenv` then `<tmp>\\revenv\\Scripts\\pip install
capstone pefile`. Used for docs/research/2026-10-12-*.md. COF_HLDLL overrides the DLL path.

Usage:
  python hlre.py factory <classname>          export -> factory VA, object size, vtable VA
  python hlre.py vtable <classname> [n]        first n vtable slots (default 70) with slot offsets
  python hlre.py use <classname>               disassemble vtable slot +0xE8 (Use)
  python hlre.py slot <classname> <hexoff>     disassemble the vtable slot at that byte offset
  python hlre.py dis <va> [maxinsn]            disassemble from VA until ret/int3 (default 400 insns)
  python hlre.py xref <va>                     code locations that call/jmp/push/mov the given VA
  python hlre.py str <text>                    VAs of strings containing text, plus code refs to them
  python hlre.py exports <substring>           matching export names and VAs

Annotations: string literals referenced by immediates, known helper names, engine-function
table slots ([0x10222F48 + 4n]) and gpGlobals fields are shown as comments.
Nothing is written anywhere; the DLL is opened read-only.
"""
import os
from pathlib import Path
import struct
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

DLL = os.environ.get('COF_HLDLL', str(Path(__file__).resolve().parents[1] / 'runtimes' / 'enhanced' / 'cryoffear' / 'cl_dlls' / 'hl.dll'))

KNOWN = {
    0x10119FE0: 'UTIL_SetOrigin', 0x10119680: 'UTIL_PlayerByIndex', 0x10118590: 'UTIL_FindEntityByTargetname',
    0x10118460: 'UTIL_FindEntityByClassname', 0x10118390: 'UTIL_EntitiesInBox', 0x10017D80: 'CBaseEntity::Intersects',
    0x10101FF0: 'FireTargets', 0x10119060: 'UTIL_IsMasterTriggered', 0x100FC9F0: 'EMIT_SOUND_DYN',
    0x100DEFF0: 'CBasePlayer::EnableControl', 0x100EA5C0: 'CBasePlayer::PlayMP3', 0x10119810: 'UTIL_ProFont',
    0x10119C00: 'UTIL_ScreenFade', 0x10119C50: 'UTIL_ScreenFadeAll', 0x100125E0: 'GetClassPtr<CBasePlayer>',
    0x10102930: 'SUB_UseTargets', 0x100DA6D0: 'CBasePlayer::AddDocument', 0x100167D0: 'DispatchSpawn',
    0x10017740: 'EHANDLE::operator=', 0x100DAD40: 'AddInventoryItem', 0x100DDA70: 'DeleteInventoryItemNamed',
    0x100E4410: 'UseInventoryItem', 0x100F4090: 'CBasePlayer::Spawn',
}
ENGFUNCS = 0x10222F48
ENGFUNC_NAMES = [
    'PrecacheModel', 'PrecacheSound', 'SetModel', 'ModelIndex', 'ModelFrames', 'SetSize', 'ChangeLevel',
    'GetSpawnParms', 'SaveSpawnParms', 'VecToYaw', 'VecToAngles', 'MoveToOrigin', 'ChangeYaw', 'ChangePitch',
    'FindEntityByString', 'GetEntityIllum', 'FindEntityInSphere', 'FindClientInPVS', 'EntitiesInPVS',
    'MakeVectors', 'AngleVectors', 'CreateEntity', 'RemoveEntity', 'CreateNamedEntity', 'MakeStatic',
    'EntIsOnFloor', 'DropToFloor', 'WalkMove', 'SetOrigin', 'EmitSound', 'EmitAmbientSound', 'TraceLine',
    'TraceToss', 'TraceMonsterHull', 'TraceHull', 'TraceModel', 'TraceTexture', 'TraceSphere', 'GetAimVector',
    'ServerCommand', 'ServerExecute', 'ClientCommand', 'ParticleEffect', 'LightStyle', 'DecalIndex',
    'PointContents', 'MessageBegin', 'MessageEnd', 'WriteByte', 'WriteChar', 'WriteShort', 'WriteLong',
    'WriteAngle', 'WriteCoord', 'WriteString', 'WriteEntity', 'CVarRegister', 'CVarGetFloat', 'CVarGetString',
    'CVarSetFloat', 'CVarSetString', 'AlertMessage', 'EngineFprintf', 'PvAllocEntPrivateData',
    'PvEntPrivateData', 'FreeEntPrivateData', 'SzFromIndex', 'AllocString', 'GetVarsOfEnt',
    'PEntityOfEntOffset', 'EntOffsetOfPEntity', 'IndexOfEdict', 'PEntityOfEntIndex', 'FindEntityByVars',
    'GetModelPtr', 'RegUserMsg', 'AnimationAutomove', 'GetBonePosition', 'FunctionFromName',
    'NameForFunction', 'ClientPrintf', 'ServerPrint', 'Cmd_Args', 'Cmd_Argv', 'Cmd_Argc', 'GetAttachment',
    'CRC32_Init', 'CRC32_ProcessBuffer', 'CRC32_ProcessByte', 'CRC32_Final', 'RandomLong', 'RandomFloat',
    'SetView', 'Time', 'CrosshairAngle', 'LoadFileForMe', 'FreeFile', 'EndSection', 'CompareFileTime',
    'GetGameDir', 'Cvar_RegisterVariable', 'FadeClientVolume', 'SetClientMaxspeed', 'CreateFakeClient',
    'RunPlayerMove', 'NumberOfEntities', 'GetInfoKeyBuffer', 'InfoKeyValue', 'SetKeyValue',
    'SetClientKeyValue', 'IsMapValid', 'StaticDecal', 'PrecacheGeneric', 'GetPlayerUserId', 'BuildSoundMsg',
    'IsDedicatedServer', 'CVarGetPointer', 'GetPlayerWONId', 'Info_RemoveKey', 'GetPhysicsKeyValue',
    'SetPhysicsKeyValue', 'GetPhysicsInfoString', 'PrecacheEvent', 'PlaybackEvent', 'SetFatPVS', 'SetFatPAS',
    'CheckVisibility', 'DeltaSetField', 'DeltaUnsetField', 'DeltaAddEncoder', 'GetCurrentPlayer',
    'CanSkipPlayer', 'DeltaFindField', 'DeltaSetFieldByIndex', 'DeltaUnsetFieldByIndex', 'SetGroupMask',
    'CreateInstancedBaseline', 'Cvar_DirectSet', 'ForceUnmodified', 'GetPlayerStats', 'AddServerCommand',
]

_pe = None
_img = None
_base = None


def load():
    global _pe, _img, _base
    if _pe is None:
        _pe = pefile.PE(DLL, fast_load=False)
        _base = _pe.OPTIONAL_HEADER.ImageBase
        _img = _pe.get_memory_mapped_image()
    return _pe


def rd(va, n):
    load()
    o = va - _base
    return _img[o:o + n]


def u32(va):
    return struct.unpack('<I', rd(va, 4))[0]


def cstr(va, limit=200):
    try:
        b = rd(va, limit)
    except Exception:
        return None
    if not b:
        return None
    end = b.find(b'\0')
    if end <= 0:
        return None
    s = b[:end]
    if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return s.decode('latin-1')
    return None


def exports():
    load()
    out = {}
    for e in _pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.name:
            out[e.name.decode()] = _base + e.address
    return out


def section_of(va):
    load()
    for s in _pe.sections:
        a = _base + s.VirtualAddress
        if a <= va < a + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b'\0').decode()
    return None


MD = Cs(CS_ARCH_X86, CS_MODE_32)
MD.detail = False

_usermsgs = None


def usermsgs():
    """gmsg globals from LinkUserMessages (0x100E5FC0..): push "Name"; call [RegUserMsg]; mov [g], eax."""
    global _usermsgs
    if _usermsgs is None:
        _usermsgs = {}
        # The compiler interleaves: the mov after a call stores the PREVIOUS call's result.
        last = None
        completed = None
        for insn in MD.disasm(rd(0x100E5FC0, 0x1200), 0x100E5FC0):
            if insn.mnemonic == 'push' and insn.op_str.startswith('0x'):
                s = cstr(int(insn.op_str, 16))
                if s:
                    last = s
            elif insn.mnemonic == 'call' and insn.op_str == 'dword ptr [0x10223074]':
                completed = last
            elif insn.mnemonic == 'mov' and insn.op_str.startswith('dword ptr [0x') and insn.op_str.endswith('], eax') and completed:
                _usermsgs[int(insn.op_str[len('dword ptr ['):].split(']')[0], 16)] = completed
                completed = None
            elif insn.mnemonic == 'ret':
                break
    return _usermsgs


def annotate(insn):
    notes = []
    for tok in insn.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').replace('+', ' ').split():
        if tok.startswith('0x'):
            try:
                v = int(tok, 16)
            except ValueError:
                continue
            if v in KNOWN:
                notes.append(KNOWN[v])
            elif ENGFUNCS <= v < ENGFUNCS + 4 * len(ENGFUNC_NAMES) and (v - ENGFUNCS) % 4 == 0:
                notes.append('g_engfuncs.' + ENGFUNC_NAMES[(v - ENGFUNCS) // 4])
            elif v == 0x10223188:
                notes.append('gpGlobals')
            elif v in usermsgs():
                notes.append('gmsg' + usermsgs()[v])
            elif 0x10000000 <= v < 0x10300000:
                s = cstr(v)
                if s and len(s) >= 2:
                    notes.append(repr(s))
    return '   ; ' + ' | '.join(notes) if notes else ''


def dis(va, maxn=400, stop_on_ret=True):
    lines = []
    # Follow incremental-link thunks (a lone jmp) so callers see the real body.
    for _ in range(4):
        first = next(MD.disasm(rd(va, 16), va))
        if first.mnemonic != 'jmp' or not first.op_str.startswith('0x'):
            break
        lines.append(f'{first.address:08X}  jmp    {first.op_str}   ; thunk, followed')
        va = int(first.op_str, 16)
    data = rd(va, maxn * 8)
    n = 0
    for insn in MD.disasm(data, va):
        lines.append(f'{insn.address:08X}  {insn.mnemonic:6s} {insn.op_str}{annotate(insn)}')
        n += 1
        if stop_on_ret and insn.mnemonic in ('ret', 'int3'):
            break
        if n >= maxn:
            break
    return '\n'.join(lines)


def factory(classname):
    ex = exports()
    if classname not in ex:
        raise SystemExit(f'no export {classname}')
    f = ex[classname]
    # Exports are usually incremental-link thunks: follow leading jmps.
    for _ in range(4):
        first = next(MD.disasm(rd(f, 16), f))
        if first.mnemonic != 'jmp' or not first.op_str.startswith('0x'):
            break
        f = int(first.op_str, 16)
    # Factory: ... push size; call alloc ... mov dword ptr [reg], vtable
    size = None
    vt = None
    for insn in MD.disasm(rd(f, 600), f):
        if insn.mnemonic == 'push' and insn.op_str.startswith('0x') and size is None:
            v = int(insn.op_str, 16)
            if 0x80 <= v < 0x10000:
                size = v
        if insn.mnemonic == 'mov' and insn.op_str.startswith('dword ptr [') and ', 0x10' in insn.op_str:
            v = int(insn.op_str.split(', ')[-1], 16)
            if section_of(v) in ('.rdata', '.data'):
                vt = v
        if insn.mnemonic in ('ret',):
            break
    return f, size, vt


def vtable(classname, n=70):
    f, size, vt = factory(classname)
    return [(i * 4, u32(vt + i * 4)) for i in range(n)]


def xref(target):
    load()
    out = []
    for s in _pe.sections:
        if not (s.Characteristics & 0x20000000):
            continue
        a = _base + s.VirtualAddress
        data = s.get_data()
        needle = struct.pack('<I', target)
        i = data.find(needle)
        while i >= 0:
            out.append(('imm', a + i))
            i = data.find(needle, i + 1)
        # rel32 calls/jmps
        for j in range(len(data) - 5):
            op = data[j]
            if op in (0xE8, 0xE9):
                rel = struct.unpack_from('<i', data, j + 1)[0]
                if a + j + 5 + rel == target:
                    out.append(('call' if op == 0xE8 else 'jmp', a + j))
    return out


def find_str(text):
    load()
    out = []
    t = text.encode('latin-1')
    for s in _pe.sections:
        a = _base + s.VirtualAddress
        data = s.get_data()
        i = data.find(t)
        while i >= 0:
            # walk back to string start
            k = i
            while k > 0 and data[k - 1] != 0:
                k -= 1
            out.append(a + k)
            i = data.find(t, i + 1)
    return sorted(set(out))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    cmd = argv[1]
    if cmd == 'factory':
        f, size, vt = factory(argv[2])
        print(f'factory {f:08X} size {size and hex(size)} vtable {vt and hex(vt)}')
    elif cmd == 'vtable':
        n = int(argv[3]) if len(argv) > 3 else 70
        for off, v in vtable(argv[2], n):
            print(f'+0x{off:03X} slot {off // 4:3d}: {v:08X} {KNOWN.get(v, "")}')
    elif cmd == 'use':
        f, size, vt = factory(argv[2])
        u = u32(vt + 0xE8)
        print(f'; {argv[2]} Use = {u:08X}')
        print(dis(u))
    elif cmd == 'slot':
        f, size, vt = factory(argv[2])
        u = u32(vt + int(argv[3], 16))
        print(f'; {argv[2]} slot +{argv[3]} = {u:08X}')
        print(dis(u))
    elif cmd == 'dis':
        print(dis(int(argv[2], 16), int(argv[3]) if len(argv) > 3 else 400))
    elif cmd == 'xref':
        for kind, a in xref(int(argv[2], 16)):
            print(f'{kind:5s} {a:08X}')
    elif cmd == 'str':
        for a in find_str(argv[2]):
            print(f'{a:08X} {cstr(a)!r}')
            for kind, r in xref(a):
                print(f'   ref {kind} {r:08X}')
    elif cmd == 'exports':
        for k, v in sorted(exports().items()):
            if argv[2].lower() in k.lower():
                print(f'{v:08X} {k}')
    else:
        print(__doc__)


if __name__ == '__main__':
    main(sys.argv)
