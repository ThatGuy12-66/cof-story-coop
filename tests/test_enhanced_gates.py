"""Small, verified changes to the retail hl.dll in story co-op, each switched by a cvar (engine/server/sv_cof_gates.h,
docs/research/2026-10-18-cof-decompilation-survey.md): the bytes are written only over exactly the retail ones or our
own, and put back when story co-op ends."""
import hashlib
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_coop_engine import HL_DLL, HL_SHA, file_bytes
from tests.test_enhanced_sections import body_of, program_output

# The single-player carry limits (cof_sp_carry): CBasePlayerItem::iMaxAmmo1 (101202F0) and each ammo box's AddAmmo
# (vtable +0x11C) take their multiplayer branch after g_pGameRules->IsMultiplayer(); the jump to the single-player
# branch becomes unconditional.
CARRY = [
    ('carry-max-ammo', 0x120304, '7476', 'eb76'),
    ('carry-ammo-g43', 0x43467, '7453', 'eb53'),
    ('carry-ammo-glock', 0x45657, '7453', 'eb53'),
    ('carry-ammo-m16', 0x50BB7, '7453', 'eb53'),
    ('carry-ammo-mp5', 0x5A397, '7453', 'eb53'),
    ('carry-ammo-p345', 0x5D927, '7453', 'eb53'),
    ('carry-ammo-revolver', 0x63B57, '7453', 'eb53'),
    ('carry-ammo-rifle', 0x65E87, '7453', 'eb53'),
    ('carry-ammo-shells', 0x6CB36, '741f', 'eb1f'),
    ('carry-ammo-tmp', 0x7E0C7, '7453', 'eb53'),
    ('carry-ammo-vp70', 0x836D7, '7453', 'eb53'),
]
# CRainModify::Use (100A99A0): the jump past the deathmatch refusal becomes unconditional (cof_coop_weather).
RAIN = [('rain-modify', 0xA99C7, '7b14', 'eb14')]
# CHalfLifeMultiplay::PlayerKilled (100C9160) hands DeathNotice g_pevLastInflictor, which the game clears after each
# TakeDamage: a player killed outside it by a monster (the chainsaw boss's grab, the user's crash of 2026-10-19)
# made DeathNotice read the NULL inflictor's classname (100C7E8D). It is handed the killer instead (push ebx).
DEATH = [('death-notice', 0xC916B, 'ff7510', '539090')]


def gate_table(test):
    header = strip_includes((SERVER / 'sv_cof_gates.h').read_text(encoding='latin-1'))
    out = run_native(test, 'cof_gates_harness.c', {'gates_under_test.h': '#include <string.h>\ntypedef unsigned char byte;\n' + header})
    return [line.split()[1:] for line in program_output(out) if line.startswith('gate ')]


class GateTests(unittest.TestCase):
    def test_gates(self):
        header = strip_includes((SERVER / 'sv_cof_gates.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_gates_harness.c', {'gates_under_test.h': '#include <string.h>\ntypedef unsigned char byte;\n' + header})
        self.assertEqual(program_output(out), [
            'friendly-fire rva C8722 size 2',
            'on: 1 bytes 90 90 writes 1',
            'on again: 1 writes 1',
            'off: 1 bytes 75 06 writes 2',
            'foreign: 0 bytes 12 34 writes 2',
            'gate friendly-fire C8722 7506 9090',
        ] + [f'gate {name} {rva:X} {old} {new}' for name, rva, old, new in CARRY + RAIN + DEATH])

    def test_every_gate_was_made_from_the_retail_bytes(self):
        if not HL_DLL.exists() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        for name, rva, original, _ in gate_table(self):
            self.assertEqual(file_bytes(HL_DLL, int(rva, 16), len(original) // 2).hex(), original, name)


class GateWiringTests(unittest.TestCase):
    def test_runs_with_the_story_layer_and_closes_with_it(self):
        assert_has(self, 'SV_CoF_GatesFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))
        assert_has(self, 'SV_CoF_GatesInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_GatesClose();', body_of(SERVER / 'sv_cof_story.c', 'static void CoF_StoryCloseGate( void )'))

    def test_single_player_carry_limits_by_default(self):
        module = (SERVER / 'sv_cof_gates.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_sp_carry, "1",', module)
        assert_has(self, 'Cvar_RegisterVariable( &cof_sp_carry );', body_of(SERVER / 'sv_cof_gates.c', 'void SV_CoF_GatesInit('))
        wanted = body_of(SERVER / 'sv_cof_gates.c', 'static qboolean CoF_GateWanted(')
        assert_has(self, 'cof_sp_carry.value != 0.0f', wanted)

    def test_friendly_fire_is_off_by_default(self):
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_friendly_fire, "0",', (SERVER / 'sv_cof_gates.c').read_text(encoding='latin-1'))

    def test_a_monster_s_kill_never_crashes_the_death_notice(self):
        # a crash fix, not a choice: always on while story co-op runs (no cvar)
        wanted = body_of(SERVER / 'sv_cof_gates.c', 'static qboolean CoF_GateWanted(')
        assert_has(self, 'case COF_GATE_DEATH_NOTICE: return true;', wanted)


if __name__ == '__main__':
    unittest.main()
