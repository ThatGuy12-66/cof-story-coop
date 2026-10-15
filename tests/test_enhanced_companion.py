"""The AI companion: its decisions, its route graph, and the engine glue that drives a player with them."""
import struct
import unittest

from tests.native import ENGINE_SERVER, ROOT, run_native, strip_includes

GRAPHS = ROOT / 'runtimes/enhanced/cryoffear/maps/graphs'


def read_graph(data: bytes):
    """The test's own reading of a saved HLSDK node graph: nodes, walkable links, door links, routes."""
    graph, node_size, link_size = 8396, 88, 24
    n, links = struct.unpack_from('<ii', data, 4 + 24)
    nodes_at = 4 + graph
    links_at = nodes_at + n * node_size
    land = [struct.unpack_from('<i', data, nodes_at + i * node_size + 28)[0] & 1 for i in range(n)]
    walk = {i: set() for i in range(n)}
    kept = doors = 0
    for i in range(n):
        if not land[i]:
            continue
        count, first = struct.unpack_from('<ii', data, nodes_at + i * node_size + 32)
        for k in range(first, first + count):
            src, dest = struct.unpack_from('<ii', data, links_at + k * link_size)
            bits, = struct.unpack_from('<i', data, links_at + k * link_size + 16)
            if src != i or (bits & 0x12) != 0x02 or dest == i or not land[dest]:
                continue
            kept += 1
            doors += data[links_at + k * link_size + 12] == ord('*')
            walk[i].add(dest)
    routes = 0
    for start in range(n):
        seen, todo = {start}, [start]
        while todo:
            for dest in walk[todo.pop()]:
                if dest not in seen:
                    seen.add(dest)
                    todo.append(dest)
        routes += len(seen)
    return n, kept, doors, routes


class CompanionTests(unittest.TestCase):
    def test_brain(self):
        out = run_native(self, 'cof_brain_test.c', {
            'sv_cof_brain.h': (ENGINE_SERVER / 'sv_cof_brain.h').read_text(),
        })
        self.assertIn('brain scenarios passed', out)

    def test_nav(self):
        out = run_native(self, 'cof_nav_test.c', {
            'sv_cof_nav.h': (ENGINE_SERVER / 'sv_cof_nav.h').read_text(),
        })
        self.assertIn('nav scenarios passed', out)

    def test_nav_real_graphs(self):
        names = ['c_start', 'c_apartment1', 'c_apartment2', 'c_asylum1']
        files = [GRAPHS / f'{name}.nod' for name in names]
        if not all(path.exists() for path in files):
            self.skipTest('the game\'s node graphs are not in runtimes/enhanced')
        header, want = [], []
        for g, path in enumerate(files):
            data = path.read_bytes()
            want.append(read_graph(data))
            header.append(f'static const unsigned char graph{g}[] = {{' + ','.join(str(b) for b in data) + '};')
        header.append(f'#define GRAPHS {len(files)}')
        header.append('static const unsigned char *graph_data[] = { ' + ', '.join(f'graph{g}' for g in range(len(files))) + ' };')
        header.append('static const size_t graph_size[] = { ' + ', '.join(f'sizeof( graph{g} )' for g in range(len(files))) + ' };')
        for field, column in (('want_nodes', 0), ('want_links', 1), ('want_doors', 2), ('want_routes', 3)):
            header.append(f'static const int {field}[] = {{ ' + ', '.join(str(row[column]) for row in want) + ' };')
        out = run_native(self, 'cof_nav_real_test.c', {
            'sv_cof_nav.h': (ENGINE_SERVER / 'sv_cof_nav.h').read_text(),
            'graph_bytes.h': '\n'.join(header) + '\n',
        }, timeout=300)
        self.assertIn('real graphs passed', out)

    def test_glue(self):
        out = run_native(self, 'cof_companion_harness.c', {
            'sv_cof_brain.h': (ENGINE_SERVER / 'sv_cof_brain.h').read_text(),
            'sv_cof_nav.h': (ENGINE_SERVER / 'sv_cof_nav.h').read_text(),
            'companion_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_companion.c').read_text()),
        })
        self.assertIn('companion glue scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
