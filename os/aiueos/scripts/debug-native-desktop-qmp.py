#!/usr/bin/env python3
"""Inspect or drive an already running native desktop through local QMP.

This is a host-side debugger. It does not use VNC, a password, a guest agent,
network access, or a substitute implementation of the guest's UI/store.
For isolated commit and cold-restore assertions, use
check-native-draft-restart-qemu.py instead.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import socket
import struct
import string
import time


HERE = Path(__file__).resolve().parent
KEYS = {'2', 'space', 'backspace', 'esc', 'f5'} | set(string.ascii_lowercase)


class QMP:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(15)
        self.sock.connect(str(path))
        self.stream = self.sock.makefile('rwb', buffering=0)
        self.seq = 0
        assert 'QMP' in json.loads(self.stream.readline())
        self.call('qmp_capabilities')

    def call(self, name, arguments=None):
        self.seq += 1
        request = {'execute': name, 'id': self.seq}
        if arguments is not None:
            request['arguments'] = arguments
        self.stream.write(json.dumps(request).encode() + b'\n')
        while True:
            response = json.loads(self.stream.readline())
            if response.get('id') == self.seq:
                if 'error' in response:
                    raise RuntimeError(response['error'])
                return response['return']

    def close(self):
        self.stream.close()
        self.sock.close()


def read_state(qmp, context, output):
    for name, offset, size in (('surface.bin', 0x238000, 8192),
                               ('context.bin', 0x23a000, 8192),
                               ('fuel.bin', 8, 8)):
        path = output / name
        qmp.call('pmemsave', {'val': context+offset, 'size': size,
                             'filename': str(path)})
    surface = struct.unpack('<2048I', (output/'surface.bin').read_bytes())
    work = struct.unpack('<2048I', (output/'context.bin').read_bytes())
    count = surface[2038]
    goal = ''.join(chr(c) for c in surface[224:224+count]) if 12 <= count <= 63 else None
    signed = lambda value: value if value < 0x80000000 else value-0x100000000
    return {'magic': work[72], 'goal': goal, 'focus': surface[2],
            'keys': work[73], 'lastKey': work[77],
            'framesStarted': work[78], 'frames': work[79],
            'generation': work[80], 'restoreCode': signed(work[81]),
            'commitCode': signed(work[82]), 'draftEnabled': work[84],
            'fuelRemaining': struct.unpack('<Q', (output/'fuel.bin').read_bytes())[0]}


def wait_for_frame(qmp, context, output, prior_keys=None, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = read_state(qmp, context, output)
        if (state['magic'] == 0x31545844 and state['frames'] >= 1
                and state['frames'] == state['framesStarted']
                and (prior_keys is None or state['keys'] > prior_keys)
                and state['frames'] >= state['keys']+1):
            return state
        time.sleep(.2)
    raise TimeoutError(f'native desktop did not complete a frame: {state}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qmp-socket', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--keys', nargs='*', default=[], choices=sorted(KEYS))
    parser.add_argument('--allow-draft-write', action='store_true',
                        help='required before sending F5 to a live VM')
    parser.add_argument('--expect-goal', help='exact goal text after the final key')
    parser.add_argument('--expect-focus', type=int,
                        help='expected focused window after the final key')
    parser.add_argument('--expect-draft-enabled', type=int, choices=(0, 1),
                        help='expected native draft availability (0 or 1)')
    parser.add_argument('--expect-generation', type=int,
                        help='expected committed draft generation')
    args = parser.parse_args()
    if 'f5' in args.keys and not args.allow_draft_write:
        parser.error('F5 can write the attached VM disk; pass --allow-draft-write')
    if not args.qmp_socket.is_socket():
        parser.error('QMP socket must exist')
    image = args.kernel.read_bytes()
    rw = struct.unpack_from('<IIQQQQQQ', image, 120)
    if rw[0:2] != (1, 6) or rw[5] != 0x240000:
        parser.error('unexpected native desktop kernel context contract')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    qmp = QMP(args.qmp_socket)
    try:
        status = qmp.call('query-status')
        assert status['running'], status
        state = wait_for_frame(qmp, rw[3], output)
        events = [{'action': 'initial', 'state': state}]
        for key in args.keys:
            previous = state
            qmp.call('send-key', {'keys': [{'type': 'qcode', 'data': key}],
                                  'hold-time': 60})
            state = wait_for_frame(qmp, rw[3], output, prior_keys=previous['keys'])
            events.append({'action': key, 'state': state})
            time.sleep(.12)
        ppm = output/'desktop.ppm'
        qmp.call('screendump', {'filename': str(ppm)})
        spec = importlib.util.spec_from_file_location('desktop_observer', HERE/'check-native-desktop-qemu.py')
        observer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(observer)
        observer.ppm_png(ppm, output/'desktop.png')
        expected = {'goal': args.expect_goal, 'focus': args.expect_focus,
                    'draftEnabled': args.expect_draft_enabled,
                    'generation': args.expect_generation}
        expected = {name: value for name, value in expected.items() if value is not None}
        failures = {name: {'expected': value, 'actual': state[name]}
                    for name, value in expected.items() if state[name] != value}
        report = {'schema': 'aiueos.native-desktop-qmp-debug.v1',
                  'status': 'PASS' if not failures else 'FAIL',
                  'kernelSha256': hashlib.sha256(image).hexdigest(),
                  'screenshotSha256': hashlib.sha256(ppm.read_bytes()).hexdigest(),
                  'events': events, 'expected': expected, 'failures': failures,
                  'vncUsed': False, 'networkUsed': False,
                  'guestStoreEmulated': False}
        (output/'debug.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(state, ensure_ascii=False))
        if failures:
            parser.exit(1, f'guest state assertion failed; see {output / "debug.json"}\n')
    finally:
        qmp.close()


if __name__ == '__main__':
    main()
