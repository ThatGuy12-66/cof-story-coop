"""1.4: looping sounds on a dynamic channel, stopped with their entity (engine/server/sv_cof_dynloops.h/.c). A player's
report (2026-10-31, the story): after the forest's chainsaw encounter "The idle chainsaw noise ... replaced the idling
sound of normal enemies, their death sound too, and since that sound is on loop it kept playing"."""
import unittest

from tests.native import ENGINE_SERVER, ROOT, assert_has, run_native
from tests.test_enhanced_sections import body_of

SOUND = ROOT / 'baseline/Cry of Fear/cryoffear/sound'
CLIENT_SOUND = ENGINE_SERVER.parent / 'client/sound/s_main.c'


def c_path(path):
    return '"' + path.as_posix() + '"'


class DynLoopsTests(unittest.TestCase):
    def test_the_count_and_the_wav_check(self):
        files = [(1, SOUND / 'boss/sawer/chainsaw_loop.wav'), (0, SOUND / 'sawcrazy/death.wav'),
                 (0, SOUND / 'sawcrazy/dblsawloop.wav')]
        listed = ',\n'.join(f'\t{{ {loops}, {c_path(path)} }}' for loops, path in files)
        out = run_native(self, 'cof_dynloops_harness.c', {
            'dynloops_under_test.h': (ENGINE_SERVER / 'sv_cof_dynloops.h').read_text(encoding='latin-1'),
            'dynloops_files.h': 'static const struct { int loops; const char *path; } wav_files[] =\n{\n' + listed +
                                '\n};\n#define WAV_FILES 3\n',
        })
        self.assertIn('dynloops scenarios passed', out)
        if (SOUND / 'boss/sawer/chainsaw_loop.wav').is_file():
            self.assertIn('chainsaw_loop.wav cue=1', out)

    def test_every_sound_sent_and_every_edict_freed_is_told(self):
        start = body_of(ENGINE_SERVER / 'sv_game.c', 'void GAME_EXPORT SV_StartSound( edict_t *ent, int chan, const char *sample, float vol, float attn, int flags, int pitch )')
        sent = start.index('SV_Multicast( msg_dest, origin, NULL, false, filter );')
        self.assertGreater(start.index('SV_CoF_DynLoopSent( ent, chan, sample, flags );'), sent)
        self.assertLess(start.index('SV_CoF_VoiceSample('), sent) # the sample sent, Simon's voice included
        free = body_of(ENGINE_SERVER / 'sv_game.c', 'void SV_FreeEdict( edict_t *pEdict )')
        self.assertLess(free.index('SV_CoF_DynLoopsFreed( pEdict );'), free.index('SV_UnlinkEdict( pEdict );'))
        self.assertLess(free.index('SV_CoF_DynLoopsFreed( pEdict );'), free.index('pEdict->serialnumber++;'))
        stop = body_of(ENGINE_SERVER / 'sv_game.c', 'void SV_CoF_StopSoundNum( int entnum, int chan, const char *sample, const vec3_t origin )')
        assert_has(self, 'SND_STOP', stop)
        assert_has(self, 'SV_Multicast( MSG_ALL,', stop)  # reliable, to everyone, as the game's own stop

    def test_the_glue(self):
        glue = (ENGINE_SERVER / 'sv_cof_dynloops.c').read_text(encoding='latin-1')
        for part in ('chan != CHAN_STATIC && chan != CHAN_STREAM', 'sv.state != ss_active', 'for( i = 1; i < copies; i++ )',
                     'for( i = 0; i < e->copies; i++ )', 'FS_Open( va( DEFAULT_SOUNDPATH "%s", name ), "rb", false )',
                     'cof_dynloops_spawn == svs.spawncount', 'FBitSet( flags, SND_CHANGE_VOL | SND_CHANGE_PITCH )'):
            assert_has(self, part, glue)

    def test_a_client_stop_stops_every_copy(self):
        alter = body_of(CLIENT_SOUND, 'static int S_AlterChannel( int entnum, int channel, const sfx_t *sfx, int vol, int pitch, int flags )')
        assert_has(self, 'if( !FBitSet( flags, SND_STOP ))\n\t\t\treturn true;\n\t\tstopped = true;', alter)
        assert_has(self, 'return stopped;', alter)


if __name__ == '__main__':
    unittest.main()
