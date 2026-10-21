"""1.1.8, from the chapter-starts audit: a chapter start (the app's chapter list, not New game or Continue) began
empty-handed with the story values of earlier chapters unset, so Chapter 2 had no phone for the basement door code,
the Chapter 7 doctor never fought, and Chapter 8 had no gun for the kills that open the way. The app names the chapter
in cof_chapter_start; the server hands out the story's kit once, on the first frame of that map."""
import re
import unittest

from tests.native import ROOT, ENGINE_SERVER, assert_has, run_native
from tests.test_enhanced_sections import body_of
from cofcoop.core import CHAPTERS

START_KIT = ('weapon_mobile', 'weapon_switchblade', 'weapon_glock')
LATE_KIT = ('weapon_shotgun', 'weapon_m16')


class KitTests(unittest.TestCase):
    def kits(self):
        out = run_native(self, 'cof_chapter_kit_harness.c',
                         {'kit_under_test.h': (ENGINE_SERVER / 'sv_cof_chapter_kit.h').read_text(encoding='latin-1')})
        self.assertIn('kits printed', out)
        kits = {}
        for line in out.splitlines():
            match = re.match(r'([A-Za-z0-9_()]*):( .*)$', line)   # MSVC's cl names the file it compiled first
            if match:
                kits[match.group(1)] = match.group(2).split()
        return kits

    def test_each_chapter_gets_the_story_kit(self):
        kits = self.kits()
        self.assertEqual(kits['c_basement'], [*START_KIT, 'doctor=0', 'three=0'])
        for name in ('c_sewer3', 'c_park3', 'c_subwaysick2'):
            self.assertEqual(kits[name], [*START_KIT, 'weapon_shotgun', 'doctor=0', 'three=0'], name)
        self.assertEqual(kits['c_attic'], ['weapon_shotgun', 'weapon_p345', 'doctor=2', 'three=1'])
        for name in ('c_ending1b', 'c_ending2b', 'c_ending3b', 'c_ending1', 'c_ending2', 'c_ending3', 'c_ending5'):
            self.assertEqual(kits[name], [*LATE_KIT, 'doctor=0', 'three=1'], name)
        # the new game, the prologue and Chapter 1 hand out their own; c_bridge empties the bag; Ending 4's wheelchair
        # arms its rider
        for name in ('c_intro', 'c_nightmare', 'c_start', 'c_bridge', 'c_ending4', 'c_subway2st1', 'C_ATTIC', '', '(null)'):
            self.assertEqual(kits[name], ['none'], name)

    def test_every_kit_is_one_of_the_app_chapters(self):
        kits = self.kits()
        chapters = {map_name for _, map_name in CHAPTERS}
        for name, kit in kits.items():
            if kit != ['none']:
                self.assertIn(name, chapters)


class WiringTests(unittest.TestCase):
    def test_the_first_story_frame_hands_the_kit_out_once(self):
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text(encoding='latin-1')
        assert_has(self, 'Cvar_Get( "cof_chapter_start", "", 0,', story)
        start = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'static void CoF_StoryChapterStart( void )')
        for line in ('kit = CoF_ChapterKit( map );',
                     'for( i = 0; i < COF_CHAPTER_WEAPONS && kit->weapons[i]; i++ ) SV_CoF_PartyGrant( kit->weapons[i] );',
                     'if( kit->doctor_weapon || kit->three_slots ) SV_CoF_StatePreset( kit->doctor_weapon, kit->three_slots );',
                     'Cvar_Set( "cof_chapter_start", "" );'):
            assert_has(self, line, start)
        frame = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame( void )')
        self.assertLess(frame.index('CoF_StoryChapterStart();'), frame.index('SV_CoF_PartyFrame();'))
        self.assertLess(frame.index('CoF_StoryChapterStart();'), frame.index('SV_CoF_StateFrame();'))

    def test_the_kit_is_shared_as_if_carried(self):
        items = body_of(ENGINE_SERVER / 'sv_cof_party.c', 'static void CoF_PartyItems( void )')
        self.assertLess(items.index('party_items |= cof_party_granted;'), items.index('if( !CoF_PartyAvailable( cl )) continue;'))
        shutdown = body_of(ENGINE_SERVER / 'sv_cof_party.c', 'void SV_CoF_PartyShutdown( void )')
        assert_has(self, 'cof_party_granted = 0;', shutdown)

    def test_the_story_values_are_preset(self):
        preset = body_of(ENGINE_SERVER / 'sv_cof_state.c', 'void SV_CoF_StatePreset( int doctor_weapon, int three_slots )')
        assert_has(self, 'if( doctor_weapon == 1 || doctor_weapon == 2 ) cof_state.values.doctor_weapon = doctor_weapon;', preset)
        assert_has(self, 'if( three_slots ) cof_state.values.three_slots = 1;', preset)

    def test_the_app_asks_only_for_chapter_starts(self):
        play = (ROOT / 'tools/coop_play.py').read_text(encoding='utf-8')
        assert_has(self, 'if not cont and map_name != NEW_GAME_MAP:', play)
        assert_has(self, 'lines.append(f"cof_chapter_start {map_name}")', play)


if __name__ == '__main__':
    unittest.main()
