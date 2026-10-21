"""1.1.8, from the final review: in the think where the crazybitch takes a guest's gun, INDEXENT( 1 ) is that guest
(sv_cof_rescue.c), and her sight check (Look -> UTIL_EntitiesInBox) walks maxEntities edicts from it, up to
MAX_CLIENTS - 1 past the end of the array. The array has that many free edicts more than the game is told."""
import unittest

from tests.native import ENGINE_SERVER, assert_has


class EdictPaddingTests(unittest.TestCase):
    def test_the_edict_array_has_free_room_past_its_end(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text(encoding='latin-1')
        assert_has(self, 'svgame.globals->maxEntities = GI->max_edicts;', game)
        assert_has(self, 'svgame.edicts = Mem_Calloc( svgame.mempool, sizeof( edict_t ) * ( GI->max_edicts + MAX_CLIENTS ));', game)
        assert_has(self, 'for( int i = 0; i < GI->max_edicts + MAX_CLIENTS; i++ )\n\t\tsvgame.edicts[i].free = true;', game)

    def test_only_a_guest_in_slot_one_is_remapped(self):
        rescue = (ENGINE_SERVER / 'sv_cof_rescue.c').read_text(encoding='latin-1')
        assert_has(self, 'if( index <= 1 || index > svs.maxclients || index > MAX_CLIENTS ) return NULL;', rescue)


if __name__ == '__main__':
    unittest.main()
