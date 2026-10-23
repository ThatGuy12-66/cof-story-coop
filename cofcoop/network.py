"""Bounded IPv4 GoldSrc networking, with no activity at import time.

Query format: https://developer.valvesoftware.com/wiki/Server_queries
GoldSrc RCON reference: ReHLDS engine/sv_main.cpp (SV_Rcon, SV_ConnectionlessPacket).
UPnP: https://upnp.org/specs/gw/UPnP-gw-WANIPConnection-v1-Service.pdf

UPnP is opt-in. Only mappings created by this process can be removed through
this module; a unique description and the internal destination are rechecked.
Mappings have a finite one-day lease. Router support is not proof of Internet
reachability: a remote player still needs to query/join successfully.
"""
from __future__ import annotations

import base64
from dataclasses import dataclass
import http.client
import ipaddress
import json
import os
import queue
import re
import secrets
import socket
import ssl
import struct
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
import zlib


class NetworkError(RuntimeError):
    """A failed network operation with a message suitable for the launcher."""


class _SoapError(NetworkError):
    def __init__(self, code: str, detail: str):
        self.code = code
        super().__init__(f'Router UPnP error {code}: {detail}')


_HEADER = b'\xff\xff\xff\xff'
_SPLIT = b'\xfe\xff\xff\xff'
_MAX_MESSAGE = 65536
_HOST_LABEL = re.compile(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\Z')


@dataclass(frozen=True)
class Endpoint:
    host: str
    port: int = 27015

    def __post_init__(self):
        host = self.host.lower().rstrip('.')
        if not host or len(host) > 253 or not all(_HOST_LABEL.fullmatch(x) for x in host.split('.')):
            raise ValueError('Enter an IPv4 address or DNS hostname, optionally followed by :port.')
        if re.fullmatch(r'[0-9.]+', host):
            try:
                ipaddress.IPv4Address(host)
            except ValueError as exc:
                raise ValueError('Invalid IPv4 address.') from exc
        if isinstance(self.port, bool) or not isinstance(self.port, int) or not 1 <= self.port <= 65535:
            raise ValueError('The port must be between 1 and 65535.')
        object.__setattr__(self, 'host', host)

    def __str__(self):
        return f'{self.host}:{self.port}'


def parse_endpoint(text: str) -> Endpoint:
    """Parse IPv4/DNS host[:port] or a COF1 address-only join code."""
    value = text.strip()
    if value.upper().startswith('COF1-'):
        if len(value) > 600:
            raise ValueError('Join code is too long.')
        try:
            prefix, body, checksum = value.upper().split('-')
            payload = base64.b32decode(body + '=' * (-len(body) % 8))
            if len(checksum) != 8 or f'{zlib.crc32(payload):08X}' != checksum:
                raise ValueError('checksum')
            value = payload.decode('ascii')
        except (ValueError, UnicodeError) as exc:
            raise ValueError('Join code is damaged or incomplete; copy it again.') from exc
    if value.count(':') > 1:
        raise ValueError('Cry of Fear requires an IPv4 address or hostname, not IPv6.')
    host, sep, port_text = value.partition(':')
    if sep and not re.fullmatch(r'[0-9]{1,5}', port_text):
        raise ValueError('The port must be a number between 1 and 65535.')
    return Endpoint(host, int(port_text) if sep else 27015)


def make_join_code(endpoint: Endpoint) -> str:
    """Encode only host and port. This is an address shorthand, not encryption."""
    payload = str(endpoint).encode('ascii')
    body = base64.b32encode(payload).decode('ascii').rstrip('=')
    return f'COF1-{body}-{zlib.crc32(payload):08X}'


def _deadline(timeout: float) -> float:
    if not 0 < timeout <= 60:
        raise ValueError('Network timeout must be greater than zero and at most 60 seconds.')
    return time.monotonic() + timeout


def _remaining(deadline: float) -> float:
    left = deadline - time.monotonic()
    if left <= 0:
        raise TimeoutError('Network operation timed out.')
    return left


def _resolve(endpoint: Endpoint, deadline: float) -> tuple[str, int]:
    try:
        return str(ipaddress.IPv4Address(endpoint.host)), endpoint.port
    except ValueError:
        pass
    # getaddrinfo has no portable timeout. A daemon resolver ensures a stalled
    # system DNS request cannot block either the GUI worker or app shutdown.
    result = queue.Queue(maxsize=1)
    def resolve():
        try:
            result.put(socket.getaddrinfo(endpoint.host, endpoint.port, socket.AF_INET, socket.SOCK_DGRAM)[0][4])
        except OSError as exc:
            result.put(exc)
    threading.Thread(target=resolve, daemon=True).start()
    try:
        address = result.get(timeout=_remaining(deadline))
    except queue.Empty as exc:
        raise NetworkError('DNS lookup timed out. Try the host’s IPv4 address.') from exc
    if isinstance(address, Exception):
        raise NetworkError(f'Could not resolve {endpoint.host}. Check the hostname or use IPv4.') from address
    return address


def _recv_message(sock: socket.socket, deadline: float) -> bytes:
    """Read one ordinary or GoldSrc split connectionless message."""
    pieces = {}
    message_id = None
    expected = None
    size = 0
    for _ in range(64):
        sock.settimeout(_remaining(deadline))
        packet = sock.recv(_MAX_MESSAGE + 1)
        if len(packet) > _MAX_MESSAGE:
            raise NetworkError('The server sent an oversized packet.')
        if packet.startswith(_HEADER):
            if pieces:
                raise NetworkError('Server mixed incomplete split and ordinary packets.')
            return packet[4:]
        if not packet.startswith(_SPLIT) or len(packet) < 10:
            raise NetworkError('The server returned an invalid GoldSrc packet.')
        this_id = packet[4:8]
        # GoldSrc packs the zero-based index in the high nibble and total in
        # the low nibble. Source split/compressed packets have another layout.
        count, index = packet[8] & 15, packet[8] >> 4
        if count < 2 or index >= count:
            raise NetworkError('Unsupported split packet format; expected GoldSrc fragments.')
        if message_id is None:
            message_id, expected = this_id, count
        if this_id != message_id or count != expected:
            raise NetworkError('The server mixed different split replies.')
        fragment = packet[9:]
        if index in pieces and pieces[index] != fragment:
            raise NetworkError('Conflicting fragments in server reply.')
        if index not in pieces:
            pieces[index] = fragment
            size += len(fragment)
        if size > _MAX_MESSAGE:
            raise NetworkError('The fragmented server reply is too large.')
        if len(pieces) == count:
            merged = b''.join(pieces[i] for i in range(count))
            return merged[4:] if merged.startswith(_HEADER) else merged
    raise NetworkError('Too many fragments in server reply.')


class _Reader:
    def __init__(self, data: bytes):
        self.data, self.pos = data, 0

    def take(self, count: int) -> bytes:
        if self.pos + count > len(self.data):
            raise NetworkError('The server sent a truncated information reply.')
        result = self.data[self.pos:self.pos + count]
        self.pos += count
        return result

    def byte(self) -> int:
        return self.take(1)[0]

    def string(self) -> str:
        end = self.data.find(b'\x00', self.pos, self.pos + 4097)
        if end < 0:
            raise NetworkError('The server sent an invalid or truncated text field.')
        raw = self.take(end - self.pos + 1)[:-1]
        try:
            return raw.decode('utf-8')
        except UnicodeDecodeError:
            return raw.decode('cp1252', errors='replace')


def _parse_info(payload: bytes) -> dict:
    reader = _Reader(payload)
    kind = reader.byte()
    result = {}
    if kind == ord('m'):
        result['engine'] = 'GoldSrc'
        result['reported_address'] = reader.string()
        for key in ('name', 'map', 'folder', 'game'):
            result[key] = reader.string()
        for key in ('players', 'max_players', 'protocol'):
            result[key] = reader.byte()
        result['server_type'] = chr(reader.byte())
        result['environment'] = chr(reader.byte())
        result['password'] = bool(reader.byte())
        if reader.byte():  # Legacy mod metadata.
            result['mod_link'] = reader.string()
            result['mod_download'] = reader.string()
            reader.take(1 + 4 + 4 + 1 + 1)
        result['vac'] = bool(reader.byte())
        result['bots'] = reader.byte()
    elif kind == ord('I'):
        result['engine'] = 'A2S'
        result['protocol'] = reader.byte()
        for key in ('name', 'map', 'folder', 'game'):
            result[key] = reader.string()
        result['app_id'] = struct.unpack('<H', reader.take(2))[0]
        for key in ('players', 'max_players', 'bots'):
            result[key] = reader.byte()
        result['server_type'] = chr(reader.byte())
        result['environment'] = chr(reader.byte())
        result['password'] = bool(reader.byte())
        result['vac'] = bool(reader.byte())
        if result['app_id'] == 2400:  # The Ship inserts three bytes.
            reader.take(3)
        result['version'] = reader.string()
        # Optional EDF fields do not affect launcher joining; leave their
        # engine-specific trailing bytes uninterpreted.
    else:
        raise NetworkError(f'Unexpected server reply type 0x{kind:02x}; expected A2S information.')
    return result


def query_server(endpoint: Endpoint, timeout: float = 2) -> dict:
    """Read server info via UDP A2S_INFO, with old `details` fallback."""
    deadline = _deadline(timeout)
    started = time.monotonic()
    address = _resolve(endpoint, deadline)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(address)  # Connected UDP discards unrelated senders.
            for request in (_HEADER + b'TSource Engine Query\x00', _HEADER + b'details\x00'):
                attempt_end = min(deadline, time.monotonic() + _remaining(deadline) / 2) if request[4:5] == b'T' else deadline
                sock.send(request)
                try:
                    for _ in range(3):
                        payload = _recv_message(sock, attempt_end)
                        if payload[:1] == b'A':
                            if len(payload) != 5:
                                raise NetworkError('The server sent an invalid A2S challenge.')
                            sock.send(request + payload[1:])
                            continue
                        result = _parse_info(payload)
                        result.update(address=str(endpoint), ping_ms=round((time.monotonic() - started) * 1000, 1))
                        return result
                    raise NetworkError('The server repeatedly requested an A2S challenge.')
                except TimeoutError:
                    continue
    except (OSError, TimeoutError) as exc:
        raise NetworkError(f'No server reply from {endpoint}. Check that the host is running and UDP {endpoint.port} is reachable through the firewall/router or your shared VPN.') from exc
    raise NetworkError(f'No server reply from {endpoint}. Check the game, firewall, and UDP {endpoint.port}; for Internet play use forwarding or a shared VPN.')


def parse_info_reply(payload: bytes) -> dict:
    """A Xash `info` reply: 'info\\n' and an infostring \\key\\value..., or '<host>: wrong version'."""
    text = payload.decode('latin-1')
    if not text.startswith('info\n'):
        raise NetworkError('The server did not answer like a Cry of Fear co-op game.')
    body = text[5:].rstrip('\n')
    if not body.startswith('\\'):
        raise NetworkError('The host runs a different version of the game engine.')
    parts = body[1:].split('\\')
    if len(parts) % 2:
        raise NetworkError('The server sent a damaged reply.')
    return dict(zip(parts[::2], parts[1::2]))


def query_info(endpoint: Endpoint, timeout: float = 2) -> dict:
    """Ask a Xash server for its info string (the engine's `info <protocol>` query)."""
    deadline = _deadline(timeout)
    address = _resolve(endpoint, deadline)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(address)
            sock.send(_HEADER + b'info 49')
            return parse_info_reply(_recv_message(sock, deadline))
    except (OSError, TimeoutError) as exc:
        raise NetworkError(f'No answer from {endpoint}. Check the address, that the host’s game is running, '
                           f'and that UDP {endpoint.port} reaches the host (router port, the host’s Windows '
                           f'Firewall, or a shared VPN).') from exc


def rcon(endpoint: Endpoint, password: str, command: str, timeout: float = 2) -> str:
    """GoldSrc UDP challenge RCON for validated host/session controls.

    RCON credentials are sent in clear text by the legacy protocol. Use a
    unique session password; the launcher controls its local server by loopback.
    """
    if not re.fullmatch(r'(?:status|cmdlist|changelevel [A-Za-z0-9_][A-Za-z0-9_-]{0,63}'
                        r'|sv_lan(?: [01])?|sv_password [A-Za-z0-9_-]{1,64})', command):
        raise ValueError('RCON accepts status, cmdlist, changelevel with one map name, sv_lan with optional 0/1, '
                         'or sv_password with 1–64 ASCII letters, digits, underscores, or hyphens.')
    password_bytes = password.encode('utf-8')
    if not password or len(password_bytes) > 256 or any(ord(c) < 32 or ord(c) == 127 or c in '\\";' for c in password):
        raise ValueError('RCON password must contain 1–256 UTF-8 bytes without quotes, backslashes, semicolons, or control characters.')
    deadline = _deadline(timeout)
    address = _resolve(endpoint, deadline)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(address)
            sock.send(_HEADER + b'challenge rcon\n\x00')
            payload = _recv_message(sock, deadline)
            match = re.fullmatch(rb'A?challenge rcon (-?[0-9]{1,12})[\x00\r\n ]*', payload)
            if not match:
                raise NetworkError('The host did not return a GoldSrc RCON challenge.')
            sock.send(_HEADER + b'rcon ' + match[1] + b' "' + password_bytes + b'" ' + command.encode('ascii') + b'\n\x00')
            parts = []
            total = 0
            for _ in range(32):
                try:
                    payload = _recv_message(sock, min(deadline, time.monotonic() + .2) if parts else deadline)
                except TimeoutError:
                    if parts:
                        break
                    raise
                if payload[:1] != b'l':
                    raise NetworkError('Unexpected GoldSrc RCON output packet.')
                part = payload[1:].rstrip(b'\x00')
                total += len(part)
                if total > _MAX_MESSAGE:
                    raise NetworkError('RCON output exceeds 64 KiB.')
                parts.append(part)
            else:
                raise NetworkError('RCON output contains too many packets.')
            text = b''.join(parts).decode('utf-8', errors='replace')
            if 'bad rcon_password' in text.lower() or 'bad challenge' in text.lower():
                raise NetworkError('RCON authentication failed. Check the current host session password; no retry was sent.')
            return text
    except (OSError, TimeoutError) as exc:
        raise NetworkError('RCON timed out or the host is unavailable. Check the host port and current session; a sent map command may already have taken effect.') from exc


def local_addresses() -> list[dict]:
    """List active IPv4 addresses with LAN, Tailscale and ZeroTier labels."""
    rows = []
    if os.name == 'nt':
        script = ("$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[Text.Encoding]::UTF8; "
                  "$rows=@(Get-NetIPConfiguration | Where-Object {$_.NetAdapter.Status -eq 'Up'} | "
                  "ForEach-Object {$alias=$_.InterfaceAlias; $desc=$_.InterfaceDescription; "
                  "foreach($ip in $_.IPv4Address){[pscustomobject]@{address=$ip.IPAddress; "
                  "label=($alias+' - '+$desc)}}}); ConvertTo-Json -InputObject $rows -Compress")
        try:
            proc = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script],
                                  capture_output=True, timeout=8, check=True,
                                  creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            rows = json.loads(proc.stdout.decode('utf-8-sig')) or []
        except (OSError, subprocess.SubprocessError, ValueError):
            rows = []
    if not rows:
        try:
            rows = [{'address': item[4][0], 'label': 'Local IPv4 (interface label unavailable)'}
                    for item in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)]
        except OSError:
            pass
    output, seen = [], set()
    for row in rows:
        try:
            address = ipaddress.IPv4Address(row['address'])
        except (ValueError, KeyError, TypeError):
            continue
        if address.is_loopback or address.is_unspecified or address.is_link_local or str(address) in seen:
            continue
        seen.add(str(address))
        label = str(row.get('label', 'Local IPv4'))
        lowered = label.lower()
        kind = 'Tailscale' if 'tailscale' in lowered else 'ZeroTier' if 'zerotier' in lowered else 'LAN / adapter'
        output.append({'address': str(address), 'label': f'{kind}: {label}'})
    return output


def _http(request, deadline: float, limit: int = 131072) -> bytes:
    """Direct, bounded HTTP(S), including DNS, headers and slow body streams."""
    url = request if isinstance(request, str) else request.full_url
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username or parsed.password:
        raise NetworkError('Invalid network service URL.')
    port = parsed.port or (443 if parsed.scheme == 'https' else 80)
    address = _resolve(Endpoint(parsed.hostname, port), deadline)
    delay = _remaining(deadline)   # 1.2.6: before the socket, which the try below always closes (a spent deadline left it open)
    raw = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    transport = [raw]
    connection = None
    def expire():
        # Closing a socket from another thread is insufficient on all platforms;
        # shutdown wakes a blocked HTTP header/body read even during a slow drip.
        try:
            transport[0].shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
    timer = threading.Timer(delay, expire)
    timer.daemon = True
    timer.start()
    try:
        raw.settimeout(_remaining(deadline))
        raw.connect(address)
        if parsed.scheme == 'https':
            transport[0] = ssl.create_default_context().wrap_socket(raw, server_hostname=parsed.hostname)
        connection = http.client.HTTPConnection(parsed.hostname, port, timeout=_remaining(deadline))
        connection.sock = transport[0]
        target = urllib.parse.urlunsplit(('', '', parsed.path or '/', parsed.query, ''))
        if isinstance(request, str):
            connection.request('GET', target)
        else:
            connection.request(request.get_method(), target, body=request.data, headers=dict(request.header_items()))
        with connection.getresponse() as response:
            if 300 <= response.status < 400:
                raise NetworkError('Unexpected redirect from the network service.')
            # SOAP faults deliberately use HTTP 500; callers parse their XML.
            parts, total = [], 0
            while not response.isclosed():
                transport[0].settimeout(_remaining(deadline))
                part = response.read1(min(8192, limit + 1 - total))
                _remaining(deadline)
                if not part:
                    return b''.join(parts)
                total += len(part)
                if total > limit:
                    raise NetworkError('Network service returned an oversized reply.')
                parts.append(part)
            return b''.join(parts)
    except http.client.HTTPException as exc:
        if time.monotonic() >= deadline:
            raise TimeoutError('Network operation timed out.') from exc
        raise NetworkError('Network service returned an invalid HTTP response.') from exc
    finally:
        timer.cancel()
        if connection is not None:
            connection.close()
        transport[0].close()


def discover_public_address(timeout: float = 3) -> str:
    """Explicitly contact api.ipify.org over HTTPS for this connection's IPv4."""
    try:
        raw = _http('https://api.ipify.org', _deadline(timeout), 64).decode('ascii').strip()
        address = ipaddress.IPv4Address(raw)
        if not address.is_global:
            raise ValueError('not public')
        return str(address)
    except (OSError, ValueError, urllib.error.URLError) as exc:
        raise NetworkError('Could not discover public IPv4. Check Internet access, or use the LAN/VPN address.') from exc


_SERVICES = {'urn:schemas-upnp-org:service:WANIPConnection:1',
             'urn:schemas-upnp-org:service:WANIPConnection:2',
             'urn:schemas-upnp-org:service:WANPPPConnection:1'}
_OWNED_MAPPINGS: dict[str, dict] = {}
_MAPPING_LOCK = threading.Lock()


def _xml(data: bytes):
    if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():
        raise NetworkError('The router sent an unsupported XML document.')
    try:
        return ET.fromstring(data)
    except ET.ParseError as exc:
        raise NetworkError('The router returned invalid UPnP XML.') from exc


def _local_url(url: str, expected_host: str | None = None) -> str:
    try:
        parsed = urllib.parse.urlsplit(url)
        address = ipaddress.IPv4Address(parsed.hostname or '')
        if (parsed.scheme != 'http' or not address.is_private or address.is_loopback or address.is_unspecified
                or parsed.username or parsed.password or parsed.fragment
                or (expected_host and str(address) != expected_host)):
            raise ValueError('non-local router URL')
        _ = parsed.port  # Validate a malformed port before sending HTTP.
        return url
    except ValueError as exc:
        raise NetworkError('Router discovery supplied an invalid LAN control address.') from exc


def _outbound_ipv4(destination: str, deadline: float) -> str:
    """Ask the IPv4 routing table for a source address without sending data."""
    timeout = _remaining(deadline)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.settimeout(timeout)
        route.connect((destination, 1900))
        return route.getsockname()[0]


def _discover_gateway(deadline: float) -> dict:
    # Unbound SSDP can select a Hyper-V/WSL adapter instead of the Internet
    # route. UDP connect to a numeric public address consults routing only.
    outbound_ip = _outbound_ipv4('1.1.1.1', deadline)
    locations = []
    search_end = min(deadline, time.monotonic() + min(1.2, _remaining(deadline) / 2))
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((outbound_ip, 0))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(outbound_ip))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
        targets = ['urn:schemas-upnp-org:device:InternetGatewayDevice:1'] + sorted(_SERVICES)
        for target in targets:
            request = ('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n'
                       'MAN: "ssdp:discover"\r\nMX: 1\r\nST: ' + target + '\r\n\r\n')
            sock.settimeout(_remaining(search_end))
            sock.sendto(request.encode('ascii'), ('239.255.255.250', 1900))
        for _ in range(32):
            try:
                sock.settimeout(_remaining(search_end))
                data, sender = sock.recvfrom(8192)
            except TimeoutError:
                break
            if not data.startswith(b'HTTP/1.1 200'):
                continue
            headers = dict(line.split(':', 1) for line in data.decode('iso-8859-1').split('\r\n')[1:] if ':' in line)
            location = next((value.strip() for key, value in headers.items() if key.lower() == 'location'), '')
            try:
                location = _local_url(location, sender[0])
            except NetworkError:
                continue
            if location not in locations:
                locations.append(location)
    for location in locations[:8]:
        try:
            root = _xml(_http(location, deadline))
            host = urllib.parse.urlsplit(location).hostname
            url_base = next((node.text for node in root if node.tag.split('}')[-1] == 'URLBase' and node.text), location)
            for service in root.iter():
                if service.tag.split('}')[-1] != 'service':
                    continue
                values = {node.tag.split('}')[-1]: node.text or '' for node in service}
                if values.get('serviceType') not in _SERVICES:
                    continue
                url = _local_url(urllib.parse.urljoin(url_base, values.get('controlURL', '')), host)
                internal_ip = _outbound_ipv4(host, deadline)
                return {'control_url': url, 'service_type': values['serviceType'], 'internal_ip': internal_ip}
        except (OSError, NetworkError, urllib.error.URLError):
            continue
    raise NetworkError('No UPnP Internet gateway responded. Enable UPnP on your router, manually forward the game UDP port, or use a shared Tailscale/ZeroTier network. UPnP cannot fix double NAT or CGNAT.')


def _soap(service: dict, action: str, args: dict, deadline: float) -> dict:
    body = ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
            's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:' + action
            + ' xmlns:u="' + service['service_type'] + '">'
            + ''.join(f'<{key}>{escape(str(value))}</{key}>' for key, value in args.items())
            + '</u:' + action + '></s:Body></s:Envelope>').encode('utf-8')
    request = urllib.request.Request(service['control_url'], data=body, method='POST', headers={
        'Content-Type': 'text/xml; charset="utf-8"', 'SOAPAction': '"' + service['service_type'] + '#' + action + '"'})
    root = _xml(_http(request, deadline))
    values = {node.tag.split('}')[-1]: node.text or '' for node in root.iter()}
    if 'errorCode' in values:
        raise _SoapError(values['errorCode'], values.get('errorDescription', 'Request rejected'))
    if action + 'Response' not in values:
        raise NetworkError('The router returned an unexpected UPnP response.')
    return values


def _mapping_args(port: int) -> dict:
    return {'NewRemoteHost': '', 'NewExternalPort': port, 'NewProtocol': 'UDP'}


def _read_mapping(service: dict, port: int, deadline: float) -> dict | None:
    try:
        return _soap(service, 'GetSpecificPortMappingEntry', _mapping_args(port), deadline)
    except _SoapError as exc:
        if exc.code == '714':
            return None
        raise


def _same_mapping(remote: dict, owned: dict) -> bool:
    return (remote.get('NewInternalClient') == owned['internal_ip']
            and remote.get('NewInternalPort') == str(owned['port'])
            and remote.get('NewPortMappingDescription') == owned['description'])


def _left_by_launcher(remote: dict, service: dict, port: int) -> bool:
    """1.1.9 (L14.4): a mapping a launcher on this PC made for this port and could not remove (closed by a crash or
    by Task Manager): its description, to this PC's address and port."""
    return (remote.get('NewPortMappingDescription', '').startswith('CoF Coop ')
            and remote.get('NewInternalClient') == service['internal_ip']
            and remote.get('NewInternalPort') == str(port))


def upnp_map(port: int, timeout: float = 3) -> dict:
    """Explicitly create a UDP mapping with a one-day lease; never overwrite.

    Returns a handle for upnp_unmap. The caller should retain it for this app
    session. A successful mapping still needs a remote connection test.
    """
    Endpoint('127.0.0.1', port)
    deadline = _deadline(timeout)
    with _MAPPING_LOCK:
        try:
            service = _discover_gateway(deadline)
            external = _soap(service, 'GetExternalIPAddress', {}, deadline).get('NewExternalIPAddress', '')
            try:
                address = ipaddress.IPv4Address(external)
            except ValueError as exc:
                raise NetworkError('The router has no valid external IPv4 address.') from exc
            if not address.is_global:
                raise NetworkError(f'The router WAN address ({address}) is not public: double NAT or CGNAT is present. Use a shared VPN, or arrange forwarding/public IPv4 at the upstream router or ISP.')
            existing = _read_mapping(service, port, deadline)
            if existing is not None:
                for mapping in _OWNED_MAPPINGS.values():
                    if mapping['control_url'] == service['control_url'] and mapping['port'] == port and _same_mapping(existing, mapping):
                        return dict(mapping)
                if not _left_by_launcher(existing, service, port):
                    raise NetworkError(f'UDP {port} already has an existing router mapping. It was left unchanged; use that mapping or choose another host port.')
                # a launcher's own leftover reads as a failure otherwise: replaced by a new one-day mapping below
                try:
                    _soap(service, 'DeletePortMapping', _mapping_args(port), deadline)
                except _SoapError as exc:
                    if exc.code != '714':   # gone already (its lease ran out)
                        raise
            owner = secrets.token_hex(12)
            mapping = dict(service, external_ip=str(address), port=port, lease=86400, owner=owner,
                           description='CoF Coop ' + owner, protocol='UDP', reachability_verified=False)
            args = dict(_mapping_args(port), NewInternalPort=port, NewInternalClient=service['internal_ip'],
                        NewEnabled=1, NewPortMappingDescription=mapping['description'], NewLeaseDuration=mapping['lease'])
            try:
                _soap(service, 'AddPortMapping', args, deadline)
            except _SoapError as exc:
                if exc.code == '725':
                    raise NetworkError('This router accepts only permanent mappings. Create a manual UDP mapping in the router or use a VPN; the launcher requested a temporary one-day lease.') from exc
                raise
            _OWNED_MAPPINGS[owner] = dict(mapping)
            return dict(mapping)
        except (OSError, urllib.error.URLError) as exc:
            raise NetworkError('UPnP did not complete. Check the router mapping list before retrying: a request may have reached it. Any launcher mapping expires after one day. Use a shared VPN if the router is unavailable.') from exc


def upnp_unmap(mapping: dict, timeout: float = 3) -> dict:
    """Remove only an unchanged mapping created by this running launcher."""
    deadline = _deadline(timeout)
    with _MAPPING_LOCK:
        owner = mapping.get('owner')
        owned = _OWNED_MAPPINGS.get(owner)
        if owned is None or mapping != owned:
            raise NetworkError('This running launcher does not own that mapping; no router change was made.')
        try:
            remote = _read_mapping(owned, owned['port'], deadline)
            if remote is not None:
                if not _same_mapping(remote, owned):
                    raise NetworkError('The router mapping changed since this launcher created it. It was left untouched.')
                _soap(owned, 'DeletePortMapping', _mapping_args(owned['port']), deadline)
            del _OWNED_MAPPINGS[owner]
            return {'removed': True, 'port': owned['port'], 'already_absent': remote is None}
        except (OSError, urllib.error.URLError) as exc:
            raise NetworkError('Could not confirm UPnP removal. Check the router or retry; the launcher mapping has a one-day lease.') from exc
