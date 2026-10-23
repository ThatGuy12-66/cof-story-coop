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
        # 1.2.6: the app's release (cof_label in the session cfg) as cofver, for a guest's app to compare
        self.assertIn('Cvar_VariableString( "cof_label" )', body)
        self.assertIn('if( build[0] && label[0] ) Info_SetValueForKey( s, "cofver", label, sizeof( s ));', body)
        self.assertLess(body.index('"cofver"'), body.index('"host"'))
        self.assertRegex(story, r'Cvar_Get\( "cof_label", "", 0,')

    def test_a_model_set_on_a_freed_entity_names_its_caller(self):
        # 1.2.6: hl.dll's SET_MODEL on a freed edict is a Deploy run on a freed weapon (host log 2026-10-22 05:09:32);
        # the log names the game's own caller as an RVA of hl.dll
        source = (ENGINE_SERVER / 'sv_game.c').read_text()
        body = source[source.index('void GAME_EXPORT SV_SetModel( edict_t *ent, const char *modelname )'):]
        body = body[:body.index('if( !modelname ||')]
        self.assertIn('cofstory phase=setmodel-invalid entity=%d serial=%d model=%s caller=%s%08x', body)
        self.assertIn('SV_CoF_StoryBase()', body)
        self.assertIn('#define COF_SETMODEL_CALLER() ((uintptr_t)_ReturnAddress( ))', source)

    def test_the_party_moves_before_the_companion_decides(self):
        # live 2026-10-15: 11 of 72 companion teleports were overridden in the same frame by the
        # party's own door move; with the party first, the AI sees the leader beside it again
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        frame = story[story.index('void SV_CoF_StoryFrame('):]
        self.assertLess(frame.index('SV_CoF_PartyFrame();'), frame.index('SV_CoF_CompanionFrame();'))

    def test_a_stacked_player_still_touches_triggers(self):
        # SV_LinkEdict returns early for SOLID_NOT bodies; the stacked-player rule
        # (cof_moments_harness) must run in that branch, before the return
        world = (ENGINE_SERVER / 'sv_world.c').read_text()
        branch = world[world.index('// ignore non-solid bodies'):]
        branch = branch[:branch.index('return;')]
        self.assertIn('SV_CoF_MomentsStackedTouch( ent )', branch)
        self.assertIn('SV_TouchLinks( ent, sv_areanodes )', branch)
        self.assertIn('iTouchLinkSemaphore', branch)
        self.assertIn('touch_triggers &&', branch)
        # 1.2.6: the only link of a SOLID_NOT player's own move (SV_RunCmd's story branch) must ask for touches,
        # or the rule above never runs (players skip SV_Physics_Entity)
        pmove = (ENGINE_SERVER / 'sv_pmove.c').read_text()
        solid_not = pmove[pmove.index('else if( clent->v.solid == SOLID_NOT && SV_CoF_StoryCoopEnabled( ))'):]
        solid_not = solid_not[:solid_not.index('}')]
        self.assertIn('SV_LinkEdict( clent, !sv.playersonly );', solid_not)
        self.assertNotIn('SV_LinkEdict( clent, false )', solid_not)
        physics = (ENGINE_SERVER / 'sv_phys.c').read_text()
        self.assertIn('if( i > 0 && i <= svs.maxclients )', physics)

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
        # the dual weapon is deployed through the game's own SwitchWeapon after the command, only one the command
        # built: the player's dual weapons before it are noted first
        client = (ENGINE_SERVER / 'sv_client.c').read_text()
        call = client.index('svgame.dllFuncs.pfnClientCommand( cl->edict );')
        before = client[client.rindex('fullupdate = !Q_strcmp( Cmd_Argv( 0 ), "fullupdate" );', 0, call):call]
        noted = before.index('if( dual ) SV_CoF_RetailDualSeen( cl, &dual_seen );')
        self.assertLess(before.index('dual = SV_CoF_StoryCoopEnabled() && SV_CoF_RetailDualCommand( Cmd_Argv( 0 ));'), noted)
        self.assertLess(noted, before.index('SV_CoF_GlobalsEnter( cl );'))
        self.assertIn('if( dual ) SV_CoF_RetailDualDeploy( cl, &dual_seen );', client[call:call + 600])

    def test_quick_dual_wield_gates_match_the_retail_dll(self):
        # the quick-slot double tap: QuickSelectItem's refusal (jnz after IsMultiplayer at 100F087F) and the
        # CheckQuickDualWield it calls (jnz after IsMultiplayer at 100DCB40); both open with the command's, or neither
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        gates = {}
        for name in ('COF_QUICK_DUAL_GATE', 'COF_QUICK_DUAL_CHECK'):
            gates[name] = int(re.search(r'#define %s (0x[0-9A-Fa-f]+)' % name, story).group(1), 16)
        stock = {name: bytes(int(b, 16) for b in re.search(
            r'%s\[6\] = \{ ([^}]*) \}' % array, story).group(1).split(','))
            for name, array in (('COF_QUICK_DUAL_GATE', 'cof_quick_dual_gate_original'),
                                ('COF_QUICK_DUAL_CHECK', 'cof_quick_dual_check_original'))}
        self.assertEqual(gates, {'COF_QUICK_DUAL_GATE': 0xF0884, 'COF_QUICK_DUAL_CHECK': 0xDCB45})
        self.assertEqual(stock, {'COF_QUICK_DUAL_GATE': bytes.fromhex('0f855c010000'),
                                 'COF_QUICK_DUAL_CHECK': bytes.fromhex('0f850b030000')})
        enable = story[story.index('qboolean SV_CoF_StoryCoopEnabled( void )'):]
        enable = enable[:enable.index('\n}\n')]
        opened = enable.index('cof_dual_gate = CoF_StoryWriteGate( base + COF_DUAL_GATE')
        self.assertIn('if( cof_dual_gate && CoF_StoryWriteGate( base + COF_QUICK_DUAL_CHECK,', enable[opened:])
        self.assertIn('else CoF_StoryWriteGate( base + COF_QUICK_DUAL_CHECK, cof_gate_open, cof_quick_dual_check_original );', enable)
        close = story[story.index('static void CoF_StoryCloseGate( void )'):]
        close = close[:close.index('\n}\n')]
        self.assertIn('CoF_StoryWriteGate( cof_quick_dual_gate, cof_gate_open, cof_quick_dual_gate_original );', close)
        self.assertIn('CoF_StoryWriteGate( cof_quick_dual_check, cof_gate_open, cof_quick_dual_check_original );', close)
        if not HL_DLL.is_file() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail Cry of Fear 1.6 hl.dll is not installed locally')
        for name, rva in gates.items():
            self.assertEqual(file_bytes(HL_DLL, rva, 6), stock[name], name)
        # each right after a call to the rules' IsMultiplayer (vtable +0x20) and its test
        for name, rva in gates.items():
            self.assertEqual(file_bytes(HL_DLL, rva - 5, 5).hex(), 'ff502085c0', name)

    def test_continue_is_registered(self):
        session = (ENGINE_SERVER / 'sv_cof_session.inc').read_text()
        self.assertRegex(session, r'cof_continue = Cvar_Get\( "cof_continue", "0", 0,')


if __name__ == '__main__':
    unittest.main()
