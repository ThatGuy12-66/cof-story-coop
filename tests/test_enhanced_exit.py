"""The exit hold's engine glue and its map guard, run natively offline."""
import json
import unittest

from cofcoop.bsp import read_entity_text, parse_entities
from tests.native import ENGINE_SERVER, ROOT, run_native, strip_includes


class ExitTests(unittest.TestCase):
    def test_shipped_exit_classification(self):
        maps = ROOT / 'baseline/Cry of Fear/cryoffear/maps'
        if not maps.exists():
            self.skipTest('retail map fixtures are not installed')
        fixtures = []
        files = {}
        found_once_only = set()
        for path in sorted(maps.glob('c_*.bsp')):
            text = read_entity_text(path)
            fixture_name = path.stem + '.ent'
            files[fixture_name] = text
            entities = parse_entities(text)
            for entity in entities:
                if entity.get('classname') != 'trigger_changelevel':
                    continue
                target = entity.get('targetname')
                scripted = bool(target and any(other.get('target') == target for other in entities))
                if scripted and any(other.get('classname') == 'trigger_once' and other.get('target') == target for other in entities):
                    found_once_only.add((path.stem, entity.get('landmark', '')))
                values = [path.stem, entity.get('map', ''), entity.get('landmark', ''), fixture_name]
                fixtures.append('{' + ','.join(json.dumps(value) for value in values) + f',{int(scripted)}' + '}')
        self.assertIn(('c_apartment3', 'landfitta'), found_once_only)
        self.assertIn(('c_apartmentsick', 'landkuk'), found_once_only)
        source = (ENGINE_SERVER / 'sv_game.c').read_text()
        start = source.index('int SV_CoF_MapExitModels(')
        end = source.index('\n}\n', start) + 3
        out = run_native(self, 'cof_exit_shipped_test.c', {
            **files,
            'map_fixture.c': (ROOT / 'tests/engine/cof_exit_models_test.c').read_text(),
            'models_under_test.c': source[start:end],
            'map_cases.h': 'static const struct { const char *source, *destination, *landmark, *entities; int scripted; } cases[] = {\n' + ',\n'.join(fixtures) + '\n};\n',
        })
        self.assertIn('shipped exit cases passed', out)

    def test_exit_hold_in_the_engine(self):
        out = run_native(self, 'cof_exit_harness.c', {
            'sv_cof_exit_state.h': (ENGINE_SERVER / 'sv_cof_exit_state.h').read_text(),
            'exit_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_exit.c').read_text()),
        })
        self.assertIn('exit scenarios passed', out)

    def test_exit_guard_reads_the_map(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        start = game.index('int SV_CoF_MapExitModels(')
        end = game.index('\n}\n', start) + 3
        out = run_native(self, 'cof_exit_models_test.c', {'models_under_test.c': game[start:end]})
        self.assertIn('exit model scenarios passed', out)

    def test_engine_hooks_are_in_place(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        change = game[game.index('static void GAME_EXPORT pfnChangeLevel('):]
        change = change[:change.index('\n}\n')]
        self.assertLess(change.index('SV_CoF_ExitRequest( level, landname )'), change.index('SV_QueueChangeLevel( level, landname )'))
        alert = game[game.index('static void GAME_EXPORT pfnAlertMessage('):]
        self.assertIn('SV_CoF_ExitQuietAlert( buffer )', alert[:alert.index('\n}\n')])
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_ExitFrame();', story[story.index('void SV_CoF_StoryFrame('):])
        self.assertIn('SV_CoF_ExitReset();', story[story.index('void SV_CoF_StoryShutdown('):])


if __name__ == '__main__':
    unittest.main()
