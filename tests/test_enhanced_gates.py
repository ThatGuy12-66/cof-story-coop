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
# 1.2.5, the multiplayer sweep of the decompilation: single-player behaviour the story lost to a multiplayer branch.
# Each is the jump after g_pGameRules->IsMultiplayer() (call [eax+0x20]; test eax, eax), except spawn-strip (the
# UTIL_ProMessage all flag), booksimon-boss (je over the multiplayer boss block becomes jmp) and morphine-trip (the
# whole multiplayer test replaced).
SP = [
    ('limp-on', 0xEF9D8, '751c', '9090'),              # physics key "low" 1 below 25 health
    ('limp-off', 0xEFB23, '751c', '9090'),             # and 0 again
    ('limp-restore', 0xF16CC, '752d', '9090'),         # after a level change
    ('hints', 0xF3DC7, '0f8545020000', '909090909090'),  # ShowHint returned at once
    # a second syringe too soon: the multiplayer test becomes "the player's own syringe" (no partner heal trips)
    ('morphine-trip', 0x7AAC4, '8b0de42e221085c9742b8b01ff502085c07522', '83bef801000000752c90909090909090909090'),
    ('landing-sound', 0xEEF6D, '7522', '9090'),        # monsters hear a hard landing
    ('spawn-strip', 0xF45A8, '6a01', '6a00'),          # Spawn cleared everyone's message strip
    ('glock-brass', 0x47292, '0f859e010000', '909090909090'),
    ('intro-language', 0xA4288, '7414', 'eb14'),       # the intro card's language
    ('booksimon-boss', 0x70AB6, '0f8447010000', 'e94801000090'),  # c_ending4b's stray Simon health bar
]
# weapon_mp5 (cof_coop_mp5): the game keeps the MP5 for its mappers alone (donator level 2-4, hl.dll's Steam-ID list
# 100E4C30, the unlock check 100AF7E0): DefaultTouch (1011C74F) answers a touch with "This is for mappers only."
# (1011C8F1), GiveNamedItem (100E0A80) refuses to create it, and both vtables' SendInitMessage (1005BDA0, 1005BD30)
# remove a world MP5 for such a player. Each jump past the check becomes unconditional (the first two), or to the
# function tail past the removal (the init pair); cof_donator still carries 0/1, so hl.dll's debug commands stay
# closed.
MP5 = [
    ('mp5-touch', 0x11C8B7, '754e', 'eb4e'),
    ('mp5-give', 0xE0BA7, '7537', 'eb37'),
    ('mp5-init', 0x5BDDE, '7409', 'eb09'),
    ('mp5-init2', 0x5BD6E, '7427', 'eb27'),
]


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
        ] + [f'gate {name} {rva:X} {old} {new}' for name, rva, old, new in CARRY + RAIN + DEATH + SP + MP5])

    def test_every_gate_was_made_from_the_retail_bytes(self):
        if not HL_DLL.exists() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        for name, rva, original, _ in gate_table(self):
            self.assertEqual(file_bytes(HL_DLL, int(rva, 16), len(original) // 2).hex(), original, name)


    def test_the_single_player_gates_follow_the_multiplayer_test(self):
        if not HL_DLL.exists() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        for name, rva, _, _ in SP:
            if name in ('spawn-strip', 'morphine-trip'):
                continue
            # call dword ptr [eax+0x20]; test eax, eax  -- or --  mov eax, [eax+0x20]; call eax; test eax, eax
            self.assertIn(file_bytes(HL_DLL, rva - 5, 5).hex(), ('ff502085c0', '20ffd085c0'), name)
        # booksimon-boss: the jmp (E9, from its 5 bytes) lands where the je (0F 84, from its 6) did: 10070C03
        je = int.from_bytes(bytes.fromhex('47010000'), 'little')
        self.assertEqual(0x70AB6 + 6 + je, 0x70AB6 + 5 + int.from_bytes(bytes.fromhex('48010000'), 'little'))
        # morphine-trip: CSyringe::Inject (1007AAB0) puts pHealVictim (+0x1F8) or m_pPlayer in edi; the test that
        # replaces the multiplayer one jumps where its jne did (1007AAF9), and the trip test follows the nops
        self.assertEqual(file_bytes(HL_DLL, 0x7AAB4, 16).hex(), '8bbef801000085ff75068bbe60010000')
        self.assertEqual(file_bytes(HL_DLL, 0x7AAD5, 2).hex(), '7522')  # jne +0x22 from 1007AAD7: 1007AAF9
        self.assertEqual(0x7AAC4 + 9 + 0x2C, 0x7AAD7 + 0x22)
        self.assertEqual(file_bytes(HL_DLL, 0x7AAD7, 5).hex(), 'a188312210')  # mov eax, gpGlobals: the trip test
        # spawn-strip: push 1 (all); push 2 (colour); push " "; push this; call UTIL_ProMessage (10119810)
        self.assertEqual(file_bytes(HL_DLL, 0xF45A8, 15).hex(), '6a016a0268b05f1b1057e859520200')


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

    def test_single_player_behaviour_by_default(self):
        module = (SERVER / 'sv_cof_gates.c').read_text(encoding='latin-1')
        init = body_of(SERVER / 'sv_cof_gates.c', 'void SV_CoF_GatesInit(')
        wanted = body_of(SERVER / 'sv_cof_gates.c', 'static qboolean CoF_GateWanted(')
        for cvar in ('cof_sp_melee', 'cof_sp_limp', 'cof_sp_hints', 'cof_sp_morphine_trip', 'cof_sp_details'):
            assert_has(self, f'CVAR_DEFINE_AUTO( {cvar}, "1",', module)
            assert_has(self, f'Cvar_RegisterVariable( &{cvar} );', init)
            if cvar != 'cof_sp_melee':  # not a gate: pfnTraceModel asks SV_CoF_MeleeWalls
                assert_has(self, f'{cvar}.value != 0.0f', wanted)

    def test_monster_melee_misses_through_walls(self):
        # the multiplayer loop over each player's model (pfnTraceModel, no wall stops it) stays: it is how a monster
        # reaches a player the game made SOLID_NOT for touching another; a wall first on the line is a miss
        body = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnTraceModel(')
        check = body.index('SV_CoF_MeleeWalls( )')
        self.assertLess(body.index('SV_ClipMoveToEntity( pent, v1, mins, maxs, v2, &trace );'), check)
        self.assertLess(check, body.index('SV_ConvertTrace( ptr, &trace );'))
        assert_has(self, 'FBitSet( pent->v.flags, FL_CLIENT|FL_FAKECLIENT )', body)
        assert_has(self, 'trace_t world = SV_Move( v1, mins, maxs, v2, MOVE_NOMONSTERS, pent, false );', body)
        assert_has(self, 'if( world.fraction < trace.fraction )', body)
        walls = body_of(SERVER / 'sv_cof_gates.c', 'qboolean SV_CoF_MeleeWalls( void )')
        assert_has(self, 'cof_sp_melee.value != 0.0f && SV_CoF_StoryCoopEnabled()', walls)

    def test_a_monster_s_kill_never_crashes_the_death_notice(self):
        # a crash fix, not a choice: always on while story co-op runs (no cvar)
        wanted = body_of(SERVER / 'sv_cof_gates.c', 'static qboolean CoF_GateWanted(')
        assert_has(self, 'case COF_GATE_DEATH_NOTICE: return true;', wanted)

    def test_the_mp5_is_for_everyone_by_default(self):
        module = (SERVER / 'sv_cof_gates.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_mp5, "1",', module)
        assert_has(self, 'Cvar_RegisterVariable( &cof_coop_mp5 );', body_of(SERVER / 'sv_cof_gates.c', 'void SV_CoF_GatesInit('))
        wanted = body_of(SERVER / 'sv_cof_gates.c', 'static qboolean CoF_GateWanted(')
        for gate in ('COF_GATE_MP5_TOUCH', 'COF_GATE_MP5_GIVE', 'COF_GATE_MP5_INIT', 'COF_GATE_MP5_INIT2'):
            assert_has(self, f'case {gate}:', wanted)
        assert_has(self, 'cof_coop_mp5.value != 0.0f', wanted)


class MP5GateTests(unittest.TestCase):
    def test_the_mp5_jumps_stay_on_their_targets(self):
        if not HL_DLL.exists() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        # the touch and give jumps keep their length and operand (75 -> eb), so each still takes the branch the
        # weapon's own test took: the shared continuation after the classname checks (1011C907) and the item's
        # creation (100E0BE0), both mov eax, gpGlobals
        self.assertEqual(file_bytes(HL_DLL, 0x11C907, 5).hex(), 'a188312210')
        self.assertEqual(file_bytes(HL_DLL, 0xE0BE0, 5).hex(), 'a188312210')
        # the init pair jumps to the function tails past each removal: pop the two pushed registers, pop ebp, ret 4
        self.assertEqual(file_bytes(HL_DLL, 0x5BDE9, 6).hex(), '5b5e5dc20400')
        self.assertEqual(file_bytes(HL_DLL, 0x5BD97, 6).hex(), '5e5b5dc20400')

    def test_the_mp5_checks_test_the_donator_level(self):
        if not HL_DLL.exists() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        # each patched branch sits on hl.dll's mappers gate: the touch and the give load the level hl.dll's CmdStart
        # copied to the player (mov eax, [edi+0xC4]; cmp eax, 2), and each init function loads it from its player
        # argument (mov eax, [eax+0xC4]) and compares it with 2, 3 and 4
        self.assertEqual(file_bytes(HL_DLL, 0x11C8C4, 8).hex(), '8b87c400000083f8')  # the touch (1011C8C4)
        self.assertEqual(file_bytes(HL_DLL, 0xE0BB4, 8).hex(), '8b87c400000083f8')   # the give (100E0BB4)
        self.assertEqual(file_bytes(HL_DLL, 0x5BD3E, 8).hex(), '8b80c400000083f8')   # init2 (1005BD30)
        self.assertEqual(file_bytes(HL_DLL, 0x5BDAD, 8).hex(), '8b80c40000005383')   # init (1005BDA0): push ebx
        chain = '83f802740e83f803740983f8047404'  # cmp eax, 2; je; 3; je; 4; je
        self.assertEqual(file_bytes(HL_DLL, 0x5BD44, 15).hex(), chain)
        self.assertEqual(file_bytes(HL_DLL, 0x5BDB4, 15).hex(), chain)


if __name__ == '__main__':
    unittest.main()
