"""Control only the private loopback Enhanced development fixture."""
from pathlib import Path
import argparse
import json
import socket
import time

ROOT = Path(__file__).resolve().parents[1]


def command(text: str, timeout: float = 3.0, session: str = 'local') -> str:
    sessions = {'local': ('enhanced-local-test.json', 27017),
                'remote': ('enhanced-remote-session.json', 27015)}
    if session not in sessions:
        raise ValueError('Unknown local host session')
    filename, port = sessions[session]
    state = json.loads((ROOT / 'state' / filename).read_text())
    if state['port'] != port:
        raise ValueError('Unexpected local host port')
    if any(c in text for c in '\r\n\x00'):
        raise ValueError('One console command per request')
    secret = state['rcon_password']
    data = b'\xff\xff\xff\xffrcon ' + secret.encode('ascii') + b' ' + text.encode('ascii') + b'\n\0'
    parts = []
    deadline = time.monotonic() + timeout
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(('127.0.0.1', port))
        sock.send(data)
        while time.monotonic() < deadline:
            sock.settimeout(min(.3 if parts else timeout, max(.001, deadline-time.monotonic())))
            try:
                reply = sock.recv(65535)
            except TimeoutError:
                break
            if not reply.startswith(b'\xff\xff\xff\xffprint\n'):
                raise ValueError('Unexpected Enhanced RCON reply')
            parts.append(reply[10:].rstrip(b'\0').decode('utf-8', errors='replace'))
    return ''.join(parts).replace(secret, '[redacted]')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command')
    parser.add_argument('--session', choices=('local', 'remote'), default='local')
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    result = command(args.command, session=args.session)
    if args.out:
        target = args.out.resolve()
        if not target.is_relative_to((ROOT / 'evidence').resolve()):
            raise ValueError('Evidence output must remain under project evidence')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(result, encoding='utf-8')
    print(result or 'No console response received; action outcome unconfirmed.')
