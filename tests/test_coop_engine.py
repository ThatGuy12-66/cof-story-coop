"""Engine parts of the tester package: the build key in the info reply, Continue's cvar,
the stacked-player touch, dual wield in co-op."""
import hashlib
import re
import struct
import unittest

from tests.native import ENGINE_SERVER, ROOT

HL_DLL = ROOT / 'runtimes/enhanced/cryoffear/cl_dlls/hl.dll'
HL_SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'


def file_bytes(path, rva, count):
    """The bytes a PE image holds at an RVA, read from the file through its section table."""
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    sections, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    table = pe + 24 + optional
    for i in range(sections):
        size, address, _, raw = struct.unpack_from('<IIII', data, table + i * 40 + 8)
        if address <= rva < address + size:
            return data[raw + rva - address:raw + rva - address + count]
    raise ValueError(f'RVA {rva:#x} is in no section')


class CoopEngineTests(unittest.TestCase):
    def test_info_reply_carries_the_build(self):
        source = (ENGINE_SERVER / 'sv_client.c').read_text()
        body = source[source.index('static void SV_Info('):source.index('static void SV_ConnectNatClient')]
        self.assertIn('Cvar_VariableString( "cof_build" )', body)
        self.assertLess(body.index('"cofbuild"'), body.index('"host"'))   # before the hostname, which is cut to fit
        self.assertLess(body.index('"bots"'), body.index('"host"'))       # the AI companions, for the host's status
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertRegex(story, r'Cvar_Get\( "cof_build", "", 0,')

    def test_a_stacked_player_still_touches_triggers(self):
        # SV_LinkEdict returns early for SOLID_NOT bodies; the stacked-player rule
        # (cof_moments_harness) must run in that branch, before the return
        world = (ENGINE_SERVER / 'sv_world.c').read_text()
        branch = world[world.index('// ignore non-solid bodies'):]
        branch = branch[:branch.index('return;')]
        self.assertIn('SV_CoF_MomentsStackedTouch( ent )', branch)
        self.assertIn('SV_TouchLinks( ent, sv_areanodes )', branch)
        self.assertIn('iTouchLinkSemaphore', branch)

    def test_dual_wield_gate_matches_the_retail_dll(self):
        # the multiplayer refusal of `inventorydualwield` (jnz after IsMultiplayer at 1001B6DB)
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        rva = int(re.search(r'#define COF_DUAL_GATE (0x[0-9A-Fa-f]+)', story).group(1), 16)
        stock = bytes(int(b, 16) for b in re.search(
            r'cof_dual_gate_original\[6\] = \{ ([^}]*) \}', story).group(1).split(','))
        self.assertEqual((rva, stock), (0x1B6E0, bytes.fromhex('0f855a150000')))
        if not HL_DLL.is_file() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail Cry of Fear 1.6 hl.dll is not installed locally')
        self.assertEqual(file_bytes(HL_DLL, rva, 6), stock)
        # the dual weapon is deployed through the game's own SwitchWeapon after the command
        client = (ENGINE_SERVER / 'sv_client.c').read_text()
        after = client[client.index('svgame.dllFuncs.pfnClientCommand( cl->edict );'):]
        self.assertIn('SV_CoF_RetailDualDeploy( cl )', after[:600])

    def test_continue_is_registered(self):
        session = (ENGINE_SERVER / 'sv_cof_session.inc').read_text()
        self.assertRegex(session, r'cof_continue = Cvar_Get\( "cof_continue", "0", 0,')


if __name__ == '__main__':
    unittest.main()
