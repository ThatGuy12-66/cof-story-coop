"""Wire-level tests use ephemeral loopback servers; no real router is modified."""
import contextlib
import http.server
import socket
import struct
import threading
import time
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import MagicMock, patch

from cofcoop import network

HEADER = b'\xff\xff\xff\xff'
OLD_INFO = (HEADER + b'm127.0.0.1:27015\x00Test host\x00c_start\x00cryoffear\x00'
            b'Cry of Fear\x00\x02\x04\x30lw\x00\x00\x01\x00')
NEW_INFO = (HEADER + b'I\x30Test host\x00c_start\x00cryoffear\x00Cry of Fear\x00'
            + struct.pack('<H', 70) + b'\x02\x04\x00lw\x00\x01v1.6\x00')


@contextlib.contextmanager
def udp_server(handler):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 0))
    sock.settimeout(2)
    failures = []
    def run():
        try:
            handler(sock)
        except Exception as exc:
            failures.append(exc)
    worker = threading.Thread(target=run, daemon=True)
    worker.start()
    try:
        yield network.Endpoint('127.0.0.1', sock.getsockname()[1])
    finally:
        worker.join(3)
        sock.close()
        if failures:
            raise failures[0]


class EndpointTests(unittest.TestCase):
    def test_ip_and_dns_normalization(self):
        self.assertEqual(str(network.parse_endpoint('  Game.Example:27016 ')), 'game.example:27016')
        self.assertEqual(str(network.parse_endpoint('100.78.1.2')), '100.78.1.2:27015')

    def test_join_code_round_trip_and_checksum(self):
        endpoint = network.Endpoint('example.org', 28015)
        code = network.make_join_code(endpoint)
        self.assertTrue(code.startswith('COF1-'))
        self.assertEqual(network.parse_endpoint(code.lower()), endpoint)
        changed = code[:-1] + ('0' if code[-1] != '0' else '1')
        with self.assertRaises(ValueError):
            network.parse_endpoint(changed)

    def test_rejects_console_injection_bad_ports_and_ipv6(self):
        for value in ('', 'a;quit', 'a\nquit', '1.2.3.4:0', 'a:65536', '[::1]:27015',
                      'a:1:2', 'http://a', '1.2.3.999', '-hostname', 'a:abc'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                network.parse_endpoint(value)


class QueryTests(unittest.TestCase):
    def test_old_goldsrc_info(self):
        def server(sock):
            request, address = sock.recvfrom(4096)
            self.assertEqual(request, HEADER + b'TSource Engine Query\x00')
            sock.sendto(OLD_INFO, address)
        with udp_server(server) as endpoint:
            result = network.query_server(endpoint, 1)
        self.assertEqual((result['name'], result['map'], result['players'], result['max_players']),
                         ('Test host', 'c_start', 2, 4))
        self.assertEqual(result['engine'], 'GoldSrc')

    def test_new_info_challenge_echo(self):
        def server(sock):
            request, address = sock.recvfrom(4096)
            sock.sendto(HEADER + b'A\x12\x34\x56\x78', address)
            challenged, second_address = sock.recvfrom(4096)
            self.assertEqual(second_address, address)
            self.assertEqual(challenged, request + b'\x12\x34\x56\x78')
            sock.sendto(NEW_INFO, address)
        with udp_server(server) as endpoint:
            self.assertEqual(network.query_server(endpoint, 1)['map'], 'c_start')

    def test_legacy_detailed_fallback(self):
        def server(sock):
            sock.recvfrom(4096)
            request, address = sock.recvfrom(4096)
            self.assertEqual(request, HEADER + b'details\x00')
            sock.sendto(OLD_INFO, address)
        with udp_server(server) as endpoint:
            self.assertEqual(network.query_server(endpoint, .8)['players'], 2)

    def test_reassembles_out_of_order_goldsrc_split(self):
        def server(sock):
            _, address = sock.recvfrom(4096)
            split = b'\xfe\xff\xff\xff' + struct.pack('<I', 123)
            sock.sendto(split + b'\x12' + OLD_INFO[23:], address)
            sock.sendto(split + b'\x02' + OLD_INFO[:23], address)
        with udp_server(server) as endpoint:
            self.assertEqual(network.query_server(endpoint, 1)['map'], 'c_start')

    def test_truncated_packet_is_actionable_error(self):
        def server(sock):
            _, address = sock.recvfrom(4096)
            sock.sendto(HEADER + b'munterminated', address)
        with udp_server(server) as endpoint, self.assertRaises(network.NetworkError):
            network.query_server(endpoint, .3)

    def test_timeout_is_bounded(self):
        def server(sock):
            sock.recvfrom(4096)
            sock.recvfrom(4096)
        start = time.monotonic()
        with udp_server(server) as endpoint, self.assertRaises(network.NetworkError):
            network.query_server(endpoint, .15)
        self.assertLess(time.monotonic() - start, 1)


class RconTests(unittest.TestCase):
    def test_exact_cmdlist_uses_goldsrc_challenge_and_wire_command(self):
        def server(sock):
            challenge, address = sock.recvfrom(4096)
            self.assertEqual(challenge, HEADER + b'challenge rcon\n\x00')
            sock.sendto(HEADER + b'challenge rcon 77\n\x00', address)
            command, remote = sock.recvfrom(4096)
            self.assertEqual(remote, address)
            self.assertEqual(command, HEADER + b'rcon 77 "session-secret" cmdlist\n\x00')
            sock.sendto(HEADER + b'lcmdlist\nstatus\n\x00', address)
        with udp_server(server) as endpoint:
            self.assertEqual(network.rcon(endpoint, 'session-secret', 'cmdlist', 1), 'cmdlist\nstatus\n')

    def test_cmdlist_rejects_suffixes_and_injection_before_network(self):
        commands = ('cmdlist give', 'cmdlist log', 'cmdlistx', 'cmdlist ', ' cmdlist',
                    'cmdlist;quit', 'cmdlist\nquit', 'cmdlist\n', 'cmdlist\r',
                    'cmdlist\x00', 'cmdlist\t', 'cmdlist//', 'CMDLIST')
        with patch.object(network, '_resolve') as resolve:
            for command in commands:
                with self.subTest(command=command), self.assertRaises(ValueError):
                    network.rcon(network.Endpoint('127.0.0.1'), 'session-secret', command)
            resolve.assert_not_called()

    def test_lan_and_server_password_commands_use_goldsrc_wire_protocol(self):
        commands = ('sv_lan', 'sv_lan 0', 'sv_lan 1', 'sv_password -',
                    'sv_password ' + 'A9_z-' * 12 + 'aZ_0')
        for value in commands:
            with self.subTest(command=value):
                def server(sock):
                    challenge, address = sock.recvfrom(4096)
                    self.assertEqual(challenge, HEADER + b'challenge rcon\n\x00')
                    sock.sendto(HEADER + b'challenge rcon 1234\n\x00', address)
                    command, remote = sock.recvfrom(4096)
                    self.assertEqual(remote, address)
                    self.assertEqual(command, HEADER + b'rcon 1234 "session-secret" '
                                     + value.encode('ascii') + b'\n\x00')
                    sock.sendto(HEADER + b'lAccepted\n\x00', address)
                with udp_server(server) as endpoint:
                    self.assertEqual(network.rcon(endpoint, 'session-secret', value, 1), 'Accepted\n')

    def test_lan_and_server_password_reject_extra_tokens_and_injection_before_network(self):
        commands = ('sv_lan 2', 'sv_lan -1', 'sv_lan 00', 'sv_lan 0.0',
                    'sv_lan ', ' sv_lan', 'sv_lan\t0', 'sv_lan 0 extra',
                    'sv_lan 0;quit', 'sv_lan 0\nquit', 'sv_lan 0\r',
                    'sv_password', 'sv_password ', 'sv_password ' + 'a' * 65,
                    'sv_password "abc"', 'sv_password abc def',
                    'sv_password abc;quit', 'sv_password abc\nquit',
                    'sv_password abc\r', 'sv_password abc\x00',
                    'sv_password abc\\def', 'sv_password abc/def',
                    'sv_password caf\u00e9', 'sv_password abc\t', 'rcon_password abc')
        with patch.object(network, '_resolve') as resolve:
            for command in commands:
                with self.subTest(command=command), self.assertRaises(ValueError):
                    network.rcon(network.Endpoint('127.0.0.1'), 'session-secret', command)
            resolve.assert_not_called()

    def test_utf8_password_and_safe_hyphenated_map(self):
        def server(sock):
            _, address = sock.recvfrom(4096)
            sock.sendto(HEADER + b'challenge rcon 1234\n\x00', address)
            command, _ = sock.recvfrom(4096)
            self.assertEqual(command, HEADER + 'rcon 1234 "caf\u00e9" changelevel test-map\n\x00'.encode('utf-8'))
            sock.sendto(HEADER + b'lChanging level\n\x00', address)
        with udp_server(server) as endpoint:
            self.assertEqual(network.rcon(endpoint, 'caf\u00e9', 'changelevel test-map', 1), 'Changing level\n')

    def test_goldsrc_challenge_and_multipacket_reply(self):
        def server(sock):
            challenge, address = sock.recvfrom(4096)
            self.assertEqual(challenge, HEADER + b'challenge rcon\n\x00')
            sock.sendto(HEADER + b'challenge rcon 1234\n\x00', address)
            command, remote = sock.recvfrom(4096)
            self.assertEqual(remote, address)
            self.assertEqual(command, HEADER + b'rcon 1234 "test password" status\n\x00')
            sock.sendto(HEADER + b'lhostname: Test\n\x00', address)
            sock.sendto(HEADER + b'lmap: c_start\n\x00', address)
        with udp_server(server) as endpoint:
            self.assertEqual(network.rcon(endpoint, 'test password', 'status', 1),
                             'hostname: Test\nmap: c_start\n')

    def test_rejects_command_and_password_injection_before_network(self):
        for command in ('status;quit', 'status\nquit', 'changelevel ../bad', 'quit'):
            with self.subTest(command=command), self.assertRaises(ValueError):
                network.rcon(network.Endpoint('127.0.0.1'), 'password', command)
        with self.assertRaises(ValueError):
            network.rcon(network.Endpoint('127.0.0.1'), 'x";quit', 'status')

    def test_bad_password_reports_failure_without_retry(self):
        def server(sock):
            _, address = sock.recvfrom(4096)
            sock.sendto(HEADER + b'challenge rcon 42\n', address)
            sock.recvfrom(4096)
            sock.sendto(HEADER + b'lBad rcon_password.\n\x00', address)
        with udp_server(server) as endpoint, self.assertRaisesRegex(network.NetworkError, 'password'):
            network.rcon(endpoint, 'wrong', 'status', .5)


class DiscoveryTests(unittest.TestCase):
    def test_ssdp_uses_default_ipv4_route_instead_of_virtual_adapter(self):
        route, discovery, gateway_route = (MagicMock() for _ in range(3))
        for sock in (route, discovery, gateway_route):
            sock.__enter__.return_value = sock
        route.getsockname.return_value = ('192.168.1.241', 49152)
        gateway_route.getsockname.return_value = ('192.168.1.241', 49153)
        location = 'http://192.168.1.1:43359/rootDesc.xml'
        discovery.recvfrom.side_effect = [
            (('HTTP/1.1 200 OK\r\nLOCATION: ' + location + '\r\n\r\n').encode('ascii'),
             ('192.168.1.1', 1900)), TimeoutError()]
        def search_sent(packet, destination):
            # Reproduce the host's requirement: an unbound multicast search
            # chooses a virtual adapter and cannot discover the real router.
            discovery.bind.assert_called_once_with(('192.168.1.241', 0))
            self.assertIn((socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                           socket.inet_aton('192.168.1.241')),
                          [call.args for call in discovery.setsockopt.call_args_list])
            self.assertEqual(destination, ('239.255.255.250', 1900))
            self.assertTrue(packet.startswith(b'M-SEARCH * HTTP/1.1\r\n'))
        discovery.sendto.side_effect = search_sent
        description = (b'<root><device><serviceList><service>'
                       b'<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>'
                       b'<controlURL>/control</controlURL>'
                       b'</service></serviceList></device></root>')
        deadline = time.monotonic() + 3
        with patch.object(network.socket, 'socket', side_effect=[route, discovery, gateway_route]), \
                patch.object(network, '_http', return_value=description) as http:
            service = network._discover_gateway(deadline)
        self.assertEqual(service['internal_ip'], '192.168.1.241')
        self.assertEqual(service['control_url'], 'http://192.168.1.1:43359/control')
        route.connect.assert_called_once_with(('1.1.1.1', 1900))
        route.send.assert_not_called()
        route.sendto.assert_not_called()
        gateway_route.connect.assert_called_once_with(('192.168.1.1', 1900))
        http.assert_called_once_with(location, deadline)
        self.assertEqual(discovery.sendto.call_count, 1 + len(network._SERVICES))
        for sock in (route, discovery, gateway_route):
            self.assertTrue(sock.settimeout.called)
            for call in sock.settimeout.call_args_list:
                self.assertGreater(call.args[0], 0)
                self.assertLessEqual(call.args[0], 3)

    def test_expired_discovery_deadline_does_not_open_a_socket(self):
        with patch.object(network.socket, 'socket') as factory:
            with self.assertRaises(TimeoutError):
                network._discover_gateway(time.monotonic() - 1)
            factory.assert_not_called()


class Gateway(http.server.BaseHTTPRequestHandler):
    """A real local SOAP endpoint with a small IGD mapping table."""
    mapping = None
    external = '8.8.8.8'
    actions = []
    def log_message(self, *args):
        pass
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Length', '20')
        self.end_headers()
        try:
            for _ in range(20):
                self.wfile.write(b'x')
                self.wfile.flush()
                time.sleep(.03)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass
    def do_POST(self):
        data = self.rfile.read(int(self.headers['Content-Length']))
        root = ET.fromstring(data)
        action = list(list(root)[0])[0]
        name = action.tag.split('}')[-1]
        args = {item.tag.split('}')[-1]: item.text or '' for item in action}
        type(self).actions.append(name)
        result = {}
        status = 200
        if name == 'GetExternalIPAddress':
            result = {'NewExternalIPAddress': self.external}
        elif name == 'GetSpecificPortMappingEntry':
            if self.mapping is None:
                status = 500
                result = {'errorCode': '714', 'errorDescription': 'NoSuchEntryInArray'}
            else:
                result = self.mapping
        elif name == 'AddPortMapping':
            type(self).mapping = args
        elif name == 'DeletePortMapping':
            type(self).mapping = None
        else:
            raise AssertionError(name)
        outer = 'Fault' if status == 500 else name + 'Response'
        content = ('<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><'
                   + outer + '>' + ''.join('<' + key + '>' + value + '</' + key + '>'
                                            for key, value in result.items())
                   + '</' + outer + '></s:Body></s:Envelope>').encode()
        self.send_response(status)
        self.send_header('Content-Length', str(len(content)))
        self.end_headers()
        self.wfile.write(content)


class UpnpTests(unittest.TestCase):
    def setUp(self):
        Gateway.mapping = None
        Gateway.external = '8.8.8.8'
        Gateway.actions = []
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Gateway)
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()
        service = {'control_url': 'http://127.0.0.1:%s/control' % self.server.server_port,
                   'service_type': 'urn:schemas-upnp-org:service:WANIPConnection:1',
                   'internal_ip': '192.168.1.22'}
        self.discovery = patch.object(network, '_discover_gateway', return_value=service)
        self.discovery.start()
        network._OWNED_MAPPINGS.clear()

    def tearDown(self):
        self.discovery.stop()
        self.server.shutdown()
        self.server.server_close()
        self.worker.join(1)

    def test_map_and_remove_only_owned_mapping(self):
        mapping = network.upnp_map(27015, 2)
        self.assertEqual(mapping['external_ip'], '8.8.8.8')
        self.assertEqual(Gateway.mapping['NewProtocol'], 'UDP')
        self.assertEqual(Gateway.mapping['NewInternalClient'], '192.168.1.22')
        self.assertGreater(mapping['lease'], 0)
        self.assertTrue(network.upnp_unmap(mapping, 2)['removed'])
        self.assertIsNone(Gateway.mapping)

    def test_slow_stream_cannot_extend_http_deadline(self):
        start = time.monotonic()
        with self.assertRaises((TimeoutError, network.NetworkError)):
            network._http('http://127.0.0.1:%s/slow' % self.server.server_port, start + .1)
        self.assertLess(time.monotonic() - start, .4)

    def test_preexisting_mapping_untouched(self):
        existing = {'NewInternalClient': '192.168.1.22', 'NewInternalPort': '27015',
                    'NewPortMappingDescription': 'Another program', 'NewEnabled': '1',
                    'NewLeaseDuration': '0'}
        Gateway.mapping = dict(existing)
        with self.assertRaisesRegex(network.NetworkError, 'existing|already'):
            network.upnp_map(27015, 2)
        self.assertEqual(Gateway.mapping, existing)
        self.assertNotIn('AddPortMapping', Gateway.actions)

    def test_replaced_mapping_never_removed(self):
        mapping = network.upnp_map(27015, 2)
        Gateway.mapping['NewPortMappingDescription'] = 'Another program'
        with self.assertRaises(network.NetworkError):
            network.upnp_unmap(mapping, 2)
        self.assertIsNotNone(Gateway.mapping)
        self.assertNotIn('DeletePortMapping', Gateway.actions)

    def test_double_nat_rejected_before_map(self):
        Gateway.external = '100.100.1.1'
        with self.assertRaisesRegex(network.NetworkError, 'NAT|CGNAT'):
            network.upnp_map(27015, 2)
        self.assertNotIn('AddPortMapping', Gateway.actions)

    def test_forged_removal_rejected(self):
        with self.assertRaises(network.NetworkError):
            network.upnp_unmap({'port': 27015, 'owner': 'not ours'}, 2)
        self.assertEqual(Gateway.actions, [])


class InfoQueryTests(unittest.TestCase):
    """The Xash `info` query the tester app uses to check a host before joining."""

    def test_parse_info_reply(self):
        info = network.parse_info_reply(b'info\n\\p\\49\\map\\c_start\\numcl\\1\\maxcl\\2\\password\\0'
                                        b'\\cofbuild\\test1\\host\\Bob\'s game')
        self.assertEqual((info['map'], info['cofbuild'], info['host']), ('c_start', 'test1', "Bob's game"))
        with self.assertRaisesRegex(network.NetworkError, 'different version'):
            network.parse_info_reply(b"info\nBob's game: wrong version\n")
        for bad in (b'xyz', b'info\n\\odd'):
            with self.subTest(bad=bad), self.assertRaises(network.NetworkError):
                network.parse_info_reply(bad)

    def test_query_info_against_a_loopback_server(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0))
            received = []

            def answer():
                data, peer = server.recvfrom(256)
                received.append(data)
                server.sendto(HEADER + b'info\n\\p\\49\\numcl\\0\\maxcl\\4\\cofbuild\\test1', peer)
            thread = threading.Thread(target=answer)
            thread.start()
            info = network.query_info(network.Endpoint('127.0.0.1', server.getsockname()[1]), timeout=2)
            thread.join()
        self.assertEqual(received, [HEADER + b'info 49'])
        self.assertEqual((info['maxcl'], info['cofbuild']), ('4', 'test1'))

    def test_query_info_without_a_server(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as idle:
            idle.bind(('127.0.0.1', 0))
            port = idle.getsockname()[1]
        with self.assertRaises(network.NetworkError) as caught:
            network.query_info(network.Endpoint('127.0.0.1', port), timeout=0.5)
        # 1.1.8 (L7): the host's first Windows Firewall question is easily missed behind the game
        self.assertIn('the host’s Windows Firewall', str(caught.exception))


if __name__ == '__main__':
    unittest.main()
