"""The story's screen blur is one for the whole party (engine/server/sv_cof_blur.h, sv_cof_blur.c). The game's cof_blur
flips m_iBlurActive of the player who fired it, a saved field client.dll draws as a full-screen smear; in co-op the
flip that should turn it off can go to the other player, and both stay blurred for good (the 2026-10-20 player log:
c_nightmare's start blurred the host, the exit flipped the guest, both carried it into c_start)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, run_native, strip_includes
from tests.test_coop_engine import HL_DLL, HL_SHA, file_bytes
from tests.test_enhanced_inventory12 import retail
from tests.test_enhanced_sections import body_of, program_output


class BlurRuleTests(unittest.TestCase):
    def test_one_blur_for_the_party(self):
        header = strip_includes((SERVER / 'sv_cof_blur.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_blur_harness.c', {'blur_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'nightmare host: source=-1 0 - -',
            'nightmulti flips the host: source=0 1 - -',
            'the guest joins: source=-1 1 1:join -',
            'a quiet frame: source=-1 1 1 -',
            # retail: the host stays at 1 and the guest goes to 1
            'the exit flips the guest: source=1 0:flip 0 -',
            'c_start: source=-1 0 0 -',
            'solo: source=-1 0 - -',
            'solo flip 1: source=0 1 - -',
            'solo flip 2: source=0 0 - -',
            'solo flip 3: source=0 1 - -',
            'solo flip 4: source=0 0 - -',
            'carried in apart: source=-1 1 1:join 1',
            'two players: source=-1 0 0 -',
            'the guest trips: source=-1 0 1 -',
            'the story flips the host: source=0 1 1 -',
            'the trip ends: source=-1 1 1:trip-end -',
            'two again: source=-1 0 0 -',
            'the guest trips again: source=-1 0 1 -',
            'the story flips the tripper: source=1 1:flip 0 -',
            'that trip ends: source=-1 1 1:trip-end -',
            'both here: source=-1 0 0 -',
            'the guest drops: source=-1 0 - -',
            'a flip while they are gone: source=0 1 - -',
            'the guest is back: source=-1 1 1:join -',
            'three players: source=-1 0 0 0',
            'two flips at once: source=0 1 1:flip 1',
            'checkpoint host: source=-1 0:join - -',
            'checkpoint guest: source=-1 0 0:join -',
            'a flip after it: source=0 1 1:flip -',
            'carried c_nightmare: 0',
            'carried cm_start: 0',
            'carried c_tricky: 0',
            'carried c_ending1b: 1',
            'carried cm_c_ending1: 1',
            'carried cof_suicide2: 1',
            'carried c_cof_cabin: 1',
            'carried : 0',
        ])

    def test_the_fields_are_where_the_retail_code_uses_them(self):
        if not retail(HL_DLL, HL_SHA):
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        # CCofBlur::Use 100A5BB0 flips the activator's m_iBlurActive: cmp [eax+1EBC], ecx; sete cl; mov [eax+1EBC], ecx
        self.assertEqual(file_bytes(HL_DLL, 0xA5BD6, 6).hex(), '3988bc1e0000')
        self.assertEqual(file_bytes(HL_DLL, 0xA5BDF, 6).hex(), '8988bc1e0000')
        # MorphineTrip 100E8D70: cmp byte [esi+2278] (m_bIsTripping), then cmp dword [esi+1EBC], 0
        self.assertEqual(file_bytes(HL_DLL, 0xE8D79, 7).hex(), '80be7822000000')
        self.assertEqual(file_bytes(HL_DLL, 0xE8D86, 7).hex(), '83bebc1e000000')


class BlurWiringTests(unittest.TestCase):
    def test_on_by_default_with_the_story_layer(self):
        module = (SERVER / 'sv_cof_blur.c').read_text(encoding='latin-1')
        self.assertIn('CVAR_DEFINE_AUTO( cof_coop_shared_blur, "1",', module)
        self.assertIn('Cvar_RegisterVariable( &cof_coop_shared_blur );', body_of(SERVER / 'sv_cof_blur.c', 'void SV_CoF_BlurInit('))
        self.assertIn('SV_CoF_BlurInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        self.assertIn('SV_CoF_BlurFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))

    def test_the_frame_reads_and_writes_through_the_retail_layer(self):
        body = body_of(SERVER / 'sv_cof_blur.c', 'void SV_CoF_BlurFrame(')
        self.assertLess(body.index('SV_CoF_RetailBlur('), body.index('CoF_BlurShare('))
        self.assertLess(body.index('CoF_BlurShare('), body.index('SV_CoF_RetailSetBlur('))
        self.assertIn('phase=blur-shared', body)
        retail_layer = (SERVER / 'sv_cof_retail.c').read_text(encoding='latin-1')
        self.assertIn('#define COF_RETAIL_BLUR           0x1EBC', retail_layer)
        self.assertIn('#define COF_RETAIL_TRIPPING       0x2278', retail_layer)

    def test_a_level_change_leaves_the_blur_off_before_the_level_is_saved(self):
        save = (SERVER / 'sv_save.c').read_text(encoding='latin-1')
        change = save[save.index('if( !CoF_BeginStoryTransition( level, startspot )) return;'):]
        self.assertLess(change.index('SV_CoF_BlurLeaving( oldlevel );'), change.index('pSaveData = SaveGameState( true );'))
        body = body_of(SERVER / 'sv_cof_blur.c', 'void SV_CoF_BlurLeaving(')
        self.assertIn('CoF_BlurCarried( map )', body)
        self.assertIn('SV_CoF_RetailSetBlur(', body)
        self.assertIn('phase=blur-cleared', body)

    def test_a_checkpoint_starts_the_party_off(self):
        session = (SERVER / 'sv_cof_session.inc').read_text(encoding='latin-1')
        loaded = session[session.index('SV_ActivateServer( false );\n\tCoF_TransitionResume();'):]
        self.assertLess(loaded.index('SV_CoF_BlurCheckpointLoaded();'), loaded.index('phase=session-loaded'))
        frame = body_of(SERVER / 'sv_cof_blur.c', 'void SV_CoF_BlurFrame(')
        self.assertIn('CoF_BlurReset( &cof_blur, cof_blur_checkpoint == svs.spawncount && !CoF_BlurCarried( sv.name ));', frame)

    def test_the_blur_message_is_in_the_log(self):
        body = body_of(SERVER / 'sv_cof_trace.c', 'static qboolean CoF_TraceWatched(')
        self.assertIn('"Blur"', body)


if __name__ == '__main__':
    unittest.main()
