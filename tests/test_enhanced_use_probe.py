"""The door-use probe (engine/server/sv_cof_useprobe.c): when a player starts an inter_door, the log shows what the
game's +use ray met. Live 2026-10-19 (cm_college2): from inside the screwdriver classroom both doors led back into
the room; the log named only the door used, not why the room's own door brush was passed over."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


def body(text: str, start: str) -> str:
    text = text[text.index(start):]
    return text[:text.index('\n}\n')]


class UseProbeTests(unittest.TestCase):
    def test_probe_against_doubles(self):
        out = run_native(self, 'cof_use_probe_harness.c', {
            'useprobe_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_useprobe.c').read_text()),
        })
        self.assertIn('use-probe scenarios passed', out)

    def test_story_frame_probes_before_the_party_moves_anyone(self):
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        frame = body(story, 'void SV_CoF_StoryFrame(')
        self.assertIn('SV_CoF_UseProbeFrame();', frame)
        self.assertLess(frame.index('SV_CoF_UseProbeFrame();'), frame.index('SV_CoF_PartyFrame();'))
        self.assertIn('#include "sv_cof_useprobe.h"', story)


if __name__ == '__main__':
    unittest.main()
