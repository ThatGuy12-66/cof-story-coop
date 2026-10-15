"""Engine parts of the tester package: the build key in the info reply, Continue's cvar."""
import unittest

from tests.native import ENGINE_SERVER


class CoopEngineTests(unittest.TestCase):
    def test_info_reply_carries_the_build(self):
        source = (ENGINE_SERVER / 'sv_client.c').read_text()
        body = source[source.index('static void SV_Info('):source.index('static void SV_ConnectNatClient')]
        self.assertIn('Cvar_VariableString( "cof_build" )', body)
        self.assertLess(body.index('"cofbuild"'), body.index('"host"'))   # before the hostname, which is cut to fit
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertRegex(story, r'Cvar_Get\( "cof_build", "", 0,')

    def test_continue_is_registered(self):
        session = (ENGINE_SERVER / 'sv_cof_session.inc').read_text()
        self.assertRegex(session, r'cof_continue = Cvar_Get\( "cof_continue", "0", 0,')


if __name__ == '__main__':
    unittest.main()
