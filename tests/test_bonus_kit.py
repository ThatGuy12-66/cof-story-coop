"""After 1.1.9: the bonus room at every chapter start, and the wardrobe key.

A chapter start from the app's list (cof_chapter_start) lays the bonus room's items (c_start / c_bridge: inter_door
`unlockablesdoor` to `uex2`) out on the floor beside where the party starts, once, when the room is open for this game
(cof_bonus_room); per-player copies through the 1.1.9 pickups (sv_cof_pickups.c). cof_wardrobe (O) opens the wardrobe's
menu (cof_clothesmenu) for whoever presses it, anywhere (engine/server/sv_cof_bonus.c, sv_cof_bonus_kit.h)."""
import re
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of
from cofcoop.core import CHAPTERS

PREAMBLE = '#include <stddef.h>\n#include <string.h>\n'

# c_start's bonus room, as its entity lump has it (c_bridge #358-#385 the same but the MP5's frags 180): the keys a
# laid-out item gets, all but origin and angles; the package key (#894, item_key) stays out.
ROOM = {
    'weapon_famas': ({'rendercolor': '0 0 0', 'frags': '-1'}, 56),                                       # #836
    'weapon_axe': ({'rendercolor': '0 0 0', 'frags': '-1'}, 146),                                        # #867
    'weapon_booklaser': ({'renderamt': '0', 'rendermode': '0', 'renderfx': '0', 'delay': '0', 'skill': '0',
                          'rendercolor': '0 0 0', 'frags': '-1'}, -163),                                 # #895
    'weapon_camera': ({'spawnflags': '1', 'renderamt': '0', 'rendermode': '0', 'renderfx': '0', 'delay': '0',
                       'skill': '0', 'rendercolor': '0 0 0', 'frags': '-1'}, 42),                        # #896
    'item_nightvision': ({'frags': '-1'}, 37),                                                           # #884
    'weapon_mp5': ({'rendercolor': '0 0 0', 'frags': '330'}, 40),                                        # #934
}


def source(name):
    return (SERVER / name).read_text(encoding='latin-1')


def kit_table():
    text = source('sv_cof_bonus_kit.h')
    table = text[text.index('cof_bonus_kit[COF_BONUS_KIT_ITEMS] ='):text.index('};', text.index('cof_bonus_kit[COF_BONUS_KIT_ITEMS] ='))]
    rows = {}
    for match in re.finditer(r'\{ "(\w+)", \{ ((?:"[^"]*", )*)NULL \}, (-?[\d.]+)f, (\d+), (\d) \}', table.replace('\n\t\t', ' ')):
        strings = re.findall(r'"([^"]*)"', match.group(2))
        rows[match.group(1)] = (dict(zip(strings[0::2], strings[1::2])), float(match.group(3)), int(match.group(4)), int(match.group(5)))
    return rows


class KitRulesTests(unittest.TestCase):
    def test_placement_against_a_synthetic_world(self):
        out = run_native(self, 'cof_bonus_kit_test.c', {
            'bonus_under_test.h': source('sv_cof_bonus.h'),
            'bonus_kit_under_test.h': source('sv_cof_bonus_kit.h'),
        })
        self.assertIn('bonus kit placement scenarios passed', out)

    def test_the_module_against_engine_doubles(self):
        out = run_native(self, 'cof_bonus_module_harness.c', {
            'bonus_module_under_test.c': PREAMBLE + strip_includes(source('sv_cof_bonus.h')) + '\n' +
                                         strip_includes(source('sv_cof_bonus_kit.h')) + '\n' + strip_includes(source('sv_cof_bonus.c')),
        })
        self.assertIn('bonus module scenarios passed', out)

    def test_the_kit_is_the_rooms(self):
        rows = kit_table()
        self.assertEqual(list(rows), ['weapon_famas', 'weapon_axe', 'weapon_booklaser', 'weapon_camera', 'item_nightvision', 'weapon_mp5'])
        for name, (keys, yaw) in ROOM.items():
            self.assertEqual(rows[name][0], keys, name)
            self.assertEqual(rows[name][1], yaw, name)
        # the line each one's own check reads (sv_cof_bonus.h cof_bonus_lines); the MP5's is the donator level
        self.assertEqual({name: row[2] for name, row in rows.items()},
                         {'weapon_famas': 39, 'weapon_axe': 70, 'weapon_booklaser': 72, 'weapon_camera': 71,
                          'item_nightvision': 75, 'weapon_mp5': 0})
        bonus = source('sv_cof_bonus.h')
        for line, comment in ((39, 'weapon_famas'), (70, 'weapon_axe'), (71, 'weapon_camera'), (72, 'weapon_booklaser'),
                              (75, 'item_nightvision')):
            self.assertRegex(bonus, r'\{ %d, "[^\n]*\*/?[^\n]*%s' % (line, comment))
        self.assertEqual([name for name, row in rows.items() if row[3]], ['item_nightvision'])

    def test_every_later_chapter_of_the_app_gets_it(self):
        text = source('sv_cof_bonus_kit.h')
        own = re.search(r'static const char \*own\[\] = \{ ([^}]*) \};', text).group(1)
        own = set(re.findall(r'"(\w+)"', own))
        # the maps with a room of their own, and the prologue that leads straight into c_start's
        self.assertEqual(own, {'c_start', 'c_bridge', 'c_nightmare'})
        starts = [name for _, name in CHAPTERS if name != 'c_intro']   # New game names no chapter
        getting = [name for name in starts if name not in own]
        self.assertEqual(getting, ['c_basement', 'c_sewer3', 'c_park3', 'c_subwaysick2', 'c_attic', 'c_ending1b', 'c_ending2b',
                                   'c_ending3b', 'c_ending1', 'c_ending2', 'c_ending3', 'c_ending4', 'c_ending5'])


class WiringTests(unittest.TestCase):
    def test_the_chapter_start_lays_it_out_once(self):
        start = body_of(SERVER / 'sv_cof_story.c', 'static void CoF_StoryChapterStart( void )')
        # while cof_chapter_start still names the chapter, which is then cleared: once per chapter start
        self.assertLess(start.index('SV_CoF_BonusKitChapterStart( map );'), start.index('Cvar_Set( "cof_chapter_start", "" );'))
        frame = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame( void )')
        # before the pickups find the map's (the kit is theirs from the first frame)
        self.assertLess(frame.index('CoF_StoryChapterStart();'), frame.index('SV_CoF_PickupsFrame();'))
        assert_has(self, 'void SV_CoF_BonusKitChapterStart( const char *chapter );', source('sv_cof_story.h'))

    def test_the_kit_is_made_by_the_pickups_with_the_bonus_mark(self):
        kit = body_of(SERVER / 'sv_cof_bonus.c', 'void SV_CoF_BonusKitChapterStart( const char *chapter )')
        assert_has(self, 'SV_CoF_PickupsSpawnBonus( items[i], spots[i], angles, false )', kit)
        assert_has(self, 'sets = SV_CoF_PickupsEnabled() ? 1 : bound( 1, svs.maxclients, COF_BONUS_KIT_SETS );', kit)
        self.assertLess(kit.index('if( !SV_CoF_BonusRoomOpen( ))'), kit.index('SV_CoF_PickupsSpawnBonus('))
        make = body_of(SERVER / 'sv_cof_pickups.c', 'edict_t *SV_CoF_PickupsSpawnBonus(')
        self.assertLess(make.index('SetBits( ent->v.spawnflags, COF_PICKUP_BONUS_FLAG );'), make.index('svgame.dllFuncs.pfnSpawn( ent )'))
        assert_has(self, 'svgame.dllFuncs.pfnKeyValue( ent, &kvd );', make)
        copy = body_of(SERVER / 'sv_cof_pickups.c', 'static edict_t *CoF_PickupsSpawn( const cof_pickups_slot_t *slot )')
        assert_has(self, 'if( slot->bonus ) return SV_CoF_PickupsSpawnBonus( slot->bonus - 1, slot->origin, slot->angles, true );', copy)
        # the mark is a bit no weapon or item of the game reads, and not the copy mark
        rules = source('sv_cof_pickups_state.h')
        assert_has(self, '#define COF_PICKUP_BONUS_FLAG ( 1 << 29 )', rules)
        assert_has(self, '#define COF_PICKUP_COPY_FLAG ( 1 << 30 )', rules)

    def test_a_taker_does_not_touch_the_next_copy(self):
        body = body_of(SERVER / 'sv_world.c', 'static void SV_TouchLinks( edict_t *ent, areanode_t *node )')
        self.assertLess(body.index('SV_CoF_PickupsBlocked( ent, touch )'), body.index('svgame.dllFuncs.pfnTouch( touch, ent );'))
        assert_has(self, '#include "sv_cof_pickups.h"', source('sv_world.c'))

    def test_the_wardrobe_key_reaches_the_server_first(self):
        body = body_of(SERVER / 'sv_client.c', 'static void SV_ExecuteClientCommand( sv_client_t *cl, const char *s )\n{')
        hook = body.index('if( SV_CoF_BonusClientCommand( cl ))')
        self.assertLess(body.index('if( SV_CoF_SceneClientCommand( cl ))'), hook)
        self.assertLess(hook, body.index('for( i = 0; i < ARRAYSIZE( ucmds ); i++ )'))
        handler = body_of(SERVER / 'sv_cof_bonus.c', 'qboolean SV_CoF_BonusClientCommand( sv_client_t *cl )')
        assert_has(self, 'if( !Q_strcmp( cmd, "cof_wardrobe" ))', handler)
        assert_has(self, 'if( !CoF_WardrobeKeepsClose( opened, SV_CoF_SceneWardrobeUser( cl ))) return false;', handler)

    def test_the_menu_is_the_wardrobes_message(self):
        wardrobe = body_of(SERVER / 'sv_cof_bonus.c', 'static void CoF_Wardrobe( sv_client_t *cl, int slot )')
        assert_has(self, 'SV_CoF_SendUserMessage( ent, "Clothes", message, (int)sizeof( message ), true )', wardrobe)
        assert_has(self, 'flags = CoF_WardrobeFlags( settings, (size_t)length, &any );', wardrobe)
        assert_has(self, 'settings = SV_CoF_BonusLoadFile( "scriptsettings.dat", &length );', wardrobe)
        assert_has(self, '#define COF_WARDROBE_MESSAGE 7', source('sv_cof_bonus.h'))
        # the outfit worn: CBasePlayer::m_iUnlockedClothes (decompilation survey I, P+0x424)
        assert_has(self, '#define COF_RETAIL_OUTFIT         0x0424', source('sv_cof_retail.c'))

    def test_the_helpers_it_asks(self):
        assert_has(self, 'int SV_CoF_SectionOfMap( const char *map );', source('sv_cof_sections.h'))
        assert_has(self, 'return s && map && map[0] ? CoF_SectionsFind( s, map ) : -1;', body_of(SERVER / 'sv_cof_sections.c', 'int SV_CoF_SectionOfMap( const char *map )'))
        user = body_of(SERVER / 'sv_cof_scene.c', 'qboolean SV_CoF_SceneWardrobeUser( const sv_client_t *cl )')
        assert_has(self, 'CoF_SceneWardrobes();', user)
        assert_has(self, 'if( CoF_SceneWardrobeUser( camera, &user ) && user == cl->edict ) return true;', user)
        assert_has(self, 'qboolean SV_CoF_RetailAttachedCamera( sv_client_t *cl );', source('sv_cof_retail.h'))


if __name__ == '__main__':
    unittest.main()
