"""The engine's side of merged maps (engine/common/cof_sections.h): the .sections file, and the merged map's
entities put together from each member's own entities, byte for byte as the Python reference does."""
import unittest

from tests.native import ROOT, run_native
from tests.test_map_merge_bsp import MAPS, retail
from tools.map_merge import bsp, groups, merge, rules

ENGINE_COMMON = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/common'


def fnv64(data: bytes) -> int:
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def program_output(out):
    """The harness's own lines (the compiler names the file first)."""
    return [line for line in out.splitlines() if line.strip() != 'harness.c']


def header():
    return (ENGINE_COMMON / 'cof_sections.h').read_text(encoding='latin-1')


def wave1_group(name):
    retail('c_apartment3')
    g = next(x for x in groups.load_groups() if x.name == name)
    named = [(m, bsp.Bsp.read(MAPS / f'{m}.bsp')) for m in g.members]
    return g, named, groups.plan_sections(named)


class RenameKeyParityTests(unittest.TestCase):
    def test_the_engine_renames_the_keys_the_merger_does(self):
        import re
        from tools.map_merge import rules
        source = header()
        keys = re.search(r'cof_sect_ref_keys\[\] = \{(.*?)\};', source, re.S).group(1)
        self.assertEqual(tuple(re.findall(r'"([^"]+)"', keys)), rules.REF_KEYS)
        pairs = re.search(r'cof_sect_class_keys\[\]\[2\] =\s*\{(.*?)\n\};', source, re.S).group(1)
        found = re.findall(r'\{ "([^"]+)", "([^"]+)" \}', pairs)
        self.assertEqual(sorted(found), sorted((c, k) for c, ks in rules.CLASS_REF_KEYS.items() for k in ks))


class SectionsFileTests(unittest.TestCase):
    def test_parse_and_lookups_match_the_python_side(self):
        g, named, sections = wave1_group('cm_apartment3')
        text = rules.to_text(g.name, sections)
        out = run_native(self, 'cof_sections_parse_harness.c', {'sections_under_test.h': header(), 'sections.txt': text})
        expected = [f'name {g.name} count {len(sections)}']
        for i, s in enumerate(sections):
            expected.append(f'section {i} {s.map} dz {s.dz:g} first {s.first_model} models {s.models} '
                            f'zmin {s.bounds[0]:g} zmax {s.bounds[1]:g} renames {len(s.renames)} styles {len(s.styles)} '
                            f'landmarks {len(s.landmarks)}')
        expected += [f'find c_apartment4 1', f'find C_APARTMENT5 2', f'find c_apartment1 -1',
                     f'rename 1 land1 {sections[1].renames["land1"]}', 'rename 1 nothere nothere', 'rename 0 land1 land1',
                     f'style 1 32 {sections[1].styles[32]}', 'style 1 5 5',
                     'at 0 0', f'at {sections[1].bounds[0] + 10:g} 1', f'at {sections[2].bounds[1] - 10:g} 2',
                     'landmark 1 land1_m1 1', 'landmark 1 land1 0']
        self.assertEqual(program_output(out), expected)

    def test_malformed_files_are_refused(self):
        cases =['', 'cof-sections 2 cm_x\n', 'cof-sections 1 cm_x\nrename a b\n',
                 'cof-sections 1 cm_x\nsection c_a 0 1\n', 'cof-sections 1 cm_x\nsection c_a zero 1 2\n',
                 'cof-sections 1 cm_x\nsection c_a 0 1 2\nfoo bar\n',
                 'cof-sections 1 cm_x\n' + ''.join(f'section c_{k} 0 1 2\n' for k in range(5)),
                 'cof-sections 1 cm_x\nsection ' + 'c' * 64 + ' 0 1 2\n']
        out = run_native(self, 'cof_sections_refuse_harness.c', {
            'sections_under_test.h': header(),
            'cases.h': 'static const char *cases[] = {\n' + ''.join(
                '"' + c.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '",\n' for c in cases) + '};\n'})
        self.assertEqual(program_output(out), ['refused'] * len(cases))


class LoadWiringTests(unittest.TestCase):
    """Both places that read a map's entities compose a merged map's from its members."""

    def function(self, path, signature):
        text = path.read_text(encoding='latin-1')
        start = text.index(signature)
        end = text.index('\n}\n', start)
        return text, text[start:end]

    def test_the_map_loader_composes_merged_maps(self):
        text, body = self.function(ENGINE_COMMON / 'mod_bmodel.c', 'static void Mod_LoadEntities(')
        self.assertIn('#include "cof_merged.h"', text)
        merged = body.index('COM_CoF_IsMergedMap( mapname )')
        self.assertLess(merged, body.index('CoF_Lang_EntPatch( entfilename'))
        self.assertIn('COM_CoF_MergedEntities( mapname, entdata, entdatasize, &mergedsize )', body)
        self.assertIn('Host_Error', body[merged:body.index('CoF_Lang_EntPatch( entfilename')])

    def test_map_checks_exits_and_landmarks_see_the_composed_entities(self):
        server = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server'
        text, body = self.function(server / 'sv_game.c', 'static char *SV_ReadEntityScript(')
        self.assertIn('#include "cof_merged.h"', text)
        merged = body.index('COM_CoF_IsMergedMap( filename )')
        self.assertLess(body.index('lumplen = entities.filelen'), merged)
        self.assertLess(merged, body.index('CoF_Lang_EntPatch( entfilename'))
        self.assertIn('COM_CoF_MergedEntities( filename,', body)


SERVER = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server'


def body_of(path, signature):
    text = path.read_text(encoding='latin-1')
    start = text.index(signature)
    return text[start:text.index('\n}\n', start)]


class ServerWiringTests(unittest.TestCase):
    """Where the engine turns level changes into section moves and resolves original map names."""

    def test_a_door_between_sections_is_caught_before_the_exit_hold_and_the_one_change_guard(self):
        body = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnChangeLevel(')
        move = body.index('SV_CoF_SectionChangeLevel( level, landmark )')
        self.assertLess(body.index('SV_CoF_SessionWipe( level, landmark )'), move)
        self.assertLess(move, body.index('svs.spawncount == last_spawncount'))
        self.assertLess(move, body.index('SV_CoF_ExitRequest('))

    def test_a_level_change_loads_the_merged_file_with_the_landmark_named_there(self):
        body = body_of(SERVER / 'sv_game.c', 'void SV_QueueChangeLevel(')
        self.assertLess(body.index('SV_CoF_SectionsResolve( level, landname,'), body.index('Q_strncpy( mapname, level'))

    def test_the_story_transition_finds_the_source_landmark_and_exit_by_their_own_names(self):
        validate = body_of(SERVER / 'sv_save.c', 'qboolean SV_CoF_TransitionValidate(')
        self.assertIn('SV_CoF_LiveLandmark( SV_CoF_TransitionSourceLandmark( map, landmark ), origin )', validate)
        begin = body_of(SERVER / 'sv_save.c', 'static qboolean CoF_BeginStoryTransition(')
        self.assertIn('SV_CoF_LiveLandmark( SV_CoF_TransitionSourceLandmark( map, landmark ), cof_transition.source_origin )', begin)
        self.assertIn('SV_CoF_TransitionRequestedLevel( map, landmark )', begin)

    def test_the_map_command_and_level_start_use_the_asked_for_section(self):
        cmd = body_of(SERVER / 'sv_cmds.c', 'static void SV_Map_f(')
        self.assertLess(cmd.index('SV_CoF_SectionsMapCommand( mapname, sizeof( mapname ))'), cmd.index('SV_ValidateMap( mapname )'))
        load = body_of(SERVER / 'sv_init.c', 'void SV_ExecLoadLevel(')
        self.assertLess(load.index('SV_SpawnEntities('), load.index('SV_CoF_SectionsEntry()'))
        self.assertLess(load.index('SV_CoF_SectionsEntry()'), load.index('SV_ActivateServer('))

    def test_a_door_entered_off_its_usual_spot_lands_at_the_landmark_and_a_refusal_is_logged_once_a_second(self):
        # live 2026-10-16: the offset landing deep in a wall, refused (and logged) 140 times a second
        move = body_of(SERVER / 'sv_cof_sections.c', 'static void CoF_SectMove(')
        self.assertIn('CoF_SectMoveOne( s, r, anchor, landing, NULL, landmarks ? dst : NULL )', move)
        self.assertIn('CoF_SectMoveOne( s, r, i, landing, &svs.clients[anchor], landmarks ? dst : NULL )', move)
        one = body_of(SERVER / 'sv_cof_sections.c', 'static void CoF_SectMoveOne(')
        refused = one[one.index('if( !SV_CoF_PartySectionMove( cl, landing, anchor, landmark ))'):]
        self.assertLess(refused.index('if( sv.time < cof_sect_refused_at[slot] + 1.0 ) return;'), refused.index('section-move-refused'))

    def test_a_companion_carried_through_a_section_move_starts_afresh(self):
        # a teleport it had asked for before fired after the move and put it away from its person (live 2026-10-17)
        one = body_of(SERVER / 'sv_cof_sections.c', 'static void CoF_SectMoveOne(')
        self.assertLess(one.index('cof_sect_moved_at[slot] = sv.time;'), one.index('SV_CoF_CompanionMoved( cl );'))
        text = (SERVER / 'sv_cof_sections.c').read_text(encoding='latin-1')
        follow = text[text.index('if( SV_CoF_PartySectionMove( bot, leader->edict->v.origin, leader, NULL ))'):]
        self.assertLess(follow.index('SV_CoF_CompanionMoved( bot );'), follow.index('phase=section-companion'))

    def test_play_everywhere_sounds_reach_only_their_section(self):
        multicast = body_of(SERVER / 'sv_game.c', 'static int SV_Multicast(')
        # a player still connecting has no section yet: their section's loops come with their first frame
        skip = multicast.index('cof_multicast_section >= 0 && ( cl->state != cs_spawned || '
                               'SV_CoF_PlayerSection( cl->edict ) != cof_multicast_section )')
        self.assertLess(skip, multicast.index('SV_CheckClientVisiblity( cl, mask )'))
        for signature, set_line in (
                ('void GAME_EXPORT SV_StartSound(', 'cof_multicast_section = SV_CoF_SoundSection( attn, flags, origin );'),
                ('static void GAME_EXPORT pfnEmitAmbientSound(',  # an ambient loop's own section (test_enhanced_loops)
                 'cof_multicast_section = section >= 0 ? section : SV_CoF_SoundSection( attn, flags, pos );')):
            body = body_of(SERVER / 'sv_game.c', signature)
            self.assertLess(body.index(set_line), body.index('SV_Multicast('))
            self.assertLess(body.index('SV_Multicast('), body.index('cof_multicast_section = -1;'))
        module = (SERVER / 'sv_cof_sections.c').read_text(encoding='latin-1')
        sound = module[module.index('int SV_CoF_SoundSection('):]
        self.assertIn('attn > 0.0f', sound[:400])
        self.assertIn('SND_STOP', sound[:400])

    def test_a_session_keeps_its_own_merged_mode(self):
        session = SERVER / 'sv_cof_session.inc'
        save = body_of(session, 'qboolean SV_CoF_SessionSave(')
        self.assertIn('SV_CoF_SectionsSessionRecord( record, sizeof( record ))', save)
        self.assertIn('"merged.txt"', save)
        load = body_of(session, 'qboolean SV_CoF_SessionLoad(')
        restore = load.index('SV_CoF_SectionsSessionRestore(')
        self.assertLess(load.index('CoF_ArchiveFind( archive, "merged.txt"'), restore)
        self.assertLess(restore, load.index('COM_ChangeLevel( metadata->map'))

    def test_load_time_lines_reach_the_log(self):
        # a map given on the command line loads while host.allow_console is false: Con_Printf drops those lines
        # (seen live 2026-10-16: the redirect and composition lines missing, the map merged all the same)
        common = (ENGINE_COMMON / 'cof_sections.c').read_text(encoding='latin-1')
        self.assertIn('Sys_Print( buf );', common)
        server = (SERVER / 'sv_cof_sections.c').read_text(encoding='latin-1')
        for text in (common, server):
            self.assertNotIn('Con_Printf( "[cof-sections]', text)
            self.assertNotIn('Con_Printf( S_ERROR "[cof-sections]', text)

    def test_the_story_layer_runs_the_sections(self):
        init = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit(')
        self.assertIn('SV_CoF_SectionsInit();', init)
        frame = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame(')
        self.assertLess(frame.index('SV_CoF_PartyFrame();'), frame.index('SV_CoF_SectionsFrame();'))
        self.assertLess(frame.index('SV_CoF_SectionsFrame();'), frame.index('SV_CoF_CompanionFrame();'))
        module = (SERVER / 'sv_cof_sections.c').read_text(encoding='latin-1')
        self.assertIn('CVAR_DEFINE_AUTO( cof_merged_maps, "0",', module)


ROUTE_FILES = {
    # c_apartment3 + 4 + 5 as the real group names them (land1-3 clash: renamed in c_apartment4)
    'cur.txt': 'cof-sections 1 cm_apartment3\n'
               'section c_apartment3 0 1 207\nbounds -401 698\nlandmark land1\nlandmark ap5la\n'
               'section c_apartment4 1536 208 195\nbounds 1251 2234\nrename land1 land1_m1\nlandmark land1_m1\n'
               'section c_apartment5 -1536 403 30\nbounds -2000 -900\nrename ap5la ap5la_m2\nlandmark ap5la_m2\nlandmark land5\n',
    'other.txt': 'cof-sections 1 cm_apartment1\n'
                 'section c_apartment1 0 1 300\nbounds -100 900\nlandmark land5\n'
                 'section c_apartment2 1536 301 50\nbounds 1400 2500\nrename land1 land1_m1\nlandmark land1_m1\n',
}


class RouteTests(unittest.TestCase):
    """Where a change-level request goes: a section move in this file, another merged file, or a plain map."""

    def test_routes(self):
        out = run_native(self, 'cof_sections_route_harness.c', dict(ROUTE_FILES, **{'sections_under_test.h': header()}))
        self.assertEqual(program_output(out), [
            # from c_apartment3 to c_apartment4 via land1: a move to section 1, landmark land1 there is land1_m1
            'c_apartment4 land1 -> move 1 cm_apartment3 dest land1_m1 source land1',
            # back: the live landmark in section 1 is land1_m1; c_apartment3 keeps land1
            'c_apartment3 land1_m1 -> move 0 cm_apartment3 dest land1 source land1_m1',
            # c_apartment3 to c_apartment5 via ap5la, and back
            'c_apartment5 ap5la -> move 2 cm_apartment3 dest ap5la_m2 source ap5la',
            'c_apartment3 ap5la_m2 -> move 0 cm_apartment3 dest ap5la source ap5la_m2',
            # c_apartment5 to c_apartment1, a member of the other merged file
            'c_apartment1 land5 -> load 0 cm_apartment1 dest land5 source land5',
            # merging off (no other file known): the original map
            'c_apartment1 land5 off -> load -1 c_apartment1 dest land5 source land5',
            # a map in no merged file
            'c_city land9 -> load -1 c_city dest land9 source land9',
            # a door whose landmark is in no section (c_apartment2's land4): still a move, no landmark
            'c_apartment4 land4 -> move 1 cm_apartment3 dest land4 source land4',
            # unmerged current map, destination merged: section by name, landmark renamed for it
            'unmerged c_apartment2 land1 -> load 1 cm_apartment1 dest land1_m1 source land1',
        ])


class MoverTests(unittest.TestCase):
    """Who a section move takes: the players in the exit and anyone right beside them; nobody in the exit (a
    scripted change): every living player. Bots, the dead and players who just moved stay."""

    def test_movers(self):
        out = run_native(self, 'cof_sections_movers_harness.c', {'sections_under_test.h': header()})
        self.assertEqual(program_output(out), [
            'walk-in: 1 1 0 0 0 0',          # 0 in the exit, 1 beside them (100 units), 2 far, 3 a bot, 4 dead, 5 just moved
            'scripted: 1 1 1 0 0 0',         # nobody in the exit: every living person who did not just move
            'two exits: 1 0 1 0 0 0',        # 0 and 2 both in the exit, 1 is 200 away from each
            'only a bot in the exit: 1 1 1 0 0 0',
        ])


class SessionRecordTests(unittest.TestCase):
    """merged.txt in a session archive: the session's mode and the original map its first player is in."""

    def test_record(self):
        out = run_native(self, 'cof_sections_record_harness.c', {'sections_under_test.h': header()})
        self.assertEqual(program_output(out), [
            'write: merged 1|section c_apartment4|',
            'read merged on: 1 1 c_apartment4',
            'read merged off: 1 0 c_apartment1',
            'read none: 0 0 -',                 # a save from before merged maps: unmerged
            'read junk: 0 0 -',
            'read long: 0 0 -',
        ])


class ComposeTests(unittest.TestCase):
    def check_group(self, name):
        g, named, sections = wave1_group(name)
        world = merge.merged_worldspawn([x.ents[0] for _, x in named])
        expected = rules.compose_reference(world, [x.ents for _, x in named], sections)[:-1]
        files = {'sections_under_test.h': header(), 'sections.txt': rules.to_text(g.name, sections),
                 'world.ent': bsp.serialize_ents([world])[:-1].decode('latin-1')}
        for i, (m, _) in enumerate(named):
            raw = (MAPS / f'{m}.bsp').read_bytes()
            offset, length = int.from_bytes(raw[4:8], 'little'), int.from_bytes(raw[8:12], 'little')
            files[f'member{i}.ent'] = raw[offset:offset + length].split(b'\0')[0].decode('latin-1')
        files['members.h'] = f'#define MEMBERS {len(named)}\n'
        out = run_native(self, 'cof_sections_compose_harness.c', files)
        self.assertEqual(program_output(out), [f'{len(expected)} {fnv64(expected):016x}'])

    def test_a_brace_inside_a_value_stays_in_the_composed_entity(self):
        # a decal named "{ding10": the composer split the entity at that brace and the decal lost its keys
        member = ('{\n"classname" "worldspawn"\n}\n{\n"origin" "1 2 3"\n"texture" "{ding10"\n"classname" "infodecal"\n}\n'
                  '{\n"targetname" "door"\n"classname" "func_door"\n"model" "*1"\n}\n')
        section = rules.Section('c_x', 0.0, 1, 1, (0.0, 100.0))
        world = [['classname', 'worldspawn']]
        expected = rules.compose_reference(world, [bsp.parse_ents(member)], [section])[:-1]
        out = run_native(self, 'cof_sections_compose_harness.c', {
            'sections_under_test.h': header(), 'sections.txt': rules.to_text('cm_x', [section]),
            'world.ent': bsp.serialize_ents([world])[:-1].decode('latin-1'), 'member0.ent': member,
            'members.h': '#define MEMBERS 1\n#define PRINT_TEXT\n'})
        lines = program_output(out)
        self.assertEqual(lines[0], f'{len(expected)} {fnv64(expected):016x}')
        self.assertIn('"origin" "1 2 3"\n"texture" "{ding10"\n"classname" "infodecal"', out)

    def test_the_merged_files_own_entities_follow_every_members(self):
        # c_start's easter egg (rules.named_map_entities): written by the merger after the merged worldspawn, composed
        # after the members' entities so no member entity's number changes
        member = '{\n"classname" "worldspawn"\n}\n{\n"targetname" "spot"\n"classname" "info_target"\n}\n'
        section = rules.Section('c_start', 0.0, 1, 1, (0.0, 100.0))
        world = [['classname', 'worldspawn']]
        egg = [['classname', 'dave_benson_phillips'], ['origin', '613 -3156 -702']]
        expected = rules.compose_reference(world, [bsp.parse_ents(member)], [section], extras=[egg])[:-1]
        out = run_native(self, 'cof_sections_compose_harness.c', {
            'sections_under_test.h': header(), 'sections.txt': rules.to_text('cm_start', [section]),
            'world.ent': bsp.serialize_ents([world, egg])[:-1].decode('latin-1'), 'member0.ent': member,
            'members.h': '#define MEMBERS 1\n#define PRINT_TEXT\n'})
        lines = program_output(out)
        self.assertEqual(lines[0], f'{len(expected)} {fnv64(expected):016x}')
        self.assertTrue(out.rstrip().endswith('"classname" "dave_benson_phillips"\n"origin" "613 -3156 -702"\n}'), out)

    def test_apartment3_4_5_compose_like_the_reference(self):
        self.check_group('cm_apartment3')

    def test_apartment1_2_compose_like_the_reference(self):
        self.check_group('cm_apartment1')


if __name__ == '__main__':
    unittest.main()
