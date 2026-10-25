#!/usr/bin/env python3
"""Observe a real native draft commit/cold boot; never emulate the guest store.

The tiny scratch disk is test input, not a D2 workspace image. No physical disk
is accepted here. Run the native build queue around this controller as usual.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time

HERE = Path(__file__).resolve().parent
PREFIX = 'Goal local:\n'


def pin(path):
    raw = path.read_bytes()
    return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


class Guest:
    def __init__(self, args, directory, disk):
        self.directory = directory
        directory.mkdir()
        rw = struct.unpack_from('<IIQQQQQQ', args.kernel.read_bytes(), 120)
        assert rw[0:2]==(1, 6) and rw[5]==0x240000, 'unexpected kernel context contract'
        self.context = rw[3]
        command = [str(args.qemu), '-machine', 'q35,accel=tcg', '-cpu', 'max',
                   '-m', '128M', '-smp', '1', '-drive',
                   f'if=pflash,format=raw,readonly=on,file={args.firmware}',
                   '-drive', f'file={args.media},format=raw,if=ide,snapshot=on',
                   '-boot', 'order=c', '-nic', 'none', '-display', 'none',
                   '-no-reboot', '-serial', 'file:serial.log',
                   '-debugcon', 'file:debug.log',
                   '-qmp', 'unix:qmp.sock,server=on,wait=off']
        if disk:
            command += ['-device', 'isa-ide,id=draft-controller', '-drive',
                        f'file={disk},format=raw,if=none,id=draft,cache=directsync',
                        '-device', 'ide-hd,drive=draft,bus=draft-controller.0,unit=1,serial=AWAI90DRAFT000001']
        (directory/'command.json').write_text(json.dumps(command, indent=2)+'\n')
        self.log = (directory/'qemu.log').open('wb')
        self.process = subprocess.Popen(command, cwd=directory, stdout=self.log, stderr=subprocess.STDOUT)
        self.sock = None
        self.stream = None
        self.seq = 0
        try:
            end = time.monotonic()+15
            while True:
                assert self.process.poll() is None, (directory, 'QEMU exit', self.process.returncode)
                try:
                    self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    self.sock.settimeout(10)
                    previous = Path.cwd()
                    try:
                        os.chdir(directory)
                        self.sock.connect('qmp.sock')
                    finally:
                        os.chdir(previous)
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    self.sock.close()
                    assert time.monotonic()<end, 'QMP unavailable'
                    time.sleep(.1)
            self.stream = self.sock.makefile('rwb', buffering=0)
            assert 'QMP' in json.loads(self.stream.readline())
            self.call('qmp_capabilities')
        except BaseException:
            self.close(hard=True)
            raise

    def call(self, name, data=None):
        self.seq += 1
        row = dict(execute=name, id=self.seq)
        if data is not None:
            row['arguments'] = data
        self.stream.write(json.dumps(row).encode()+b'\n')
        while True:
            row = json.loads(self.stream.readline())
            if row.get('id') == self.seq:
                assert 'error' not in row, row
                return row['return']

    def hmp(self, command):
        return self.call('human-monitor-command', {'command-line': command})

    def dump(self, address, name):
        assert self.hmp(f'pmemsave 0x{address:x} 8192 {name}') == ''
        return struct.unpack('<2048I', (self.directory/name).read_bytes())

    def state(self):
        surface = self.dump(self.context+0x238000, 'surface.bin')
        context = self.dump(self.context+0x23a000, 'context.bin')
        length = surface[2038]
        signed = lambda v: v if v<0x80000000 else v-0x100000000
        return dict(goal=''.join(chr(c) for c in surface[224:224+length]) if 12<=length<=63 else None,
                    focus=surface[2], magic=context[72], keys=context[73], lastKey=context[77],
                    framesStarted=context[78], frames=context[79], generation=context[80], restore=signed(context[81]),
                    commit=signed(context[82]), enabled=context[84])

    def wait(self, name, predicate, timeout=90):
        end = time.monotonic()+timeout
        while time.monotonic()<end:
            assert self.process.poll() is None, ('guest exited', self.process.returncode)
            state = self.state()
            (self.directory/'last-state.json').write_text(json.dumps(state, indent=2)+'\n')
            # Each make event in this gate requests exactly one complete frame.
            # State mutation precedes rendering, so observing focus/goal alone
            # can capture the previous framebuffer during a cold restore.
            if (predicate(state) and state['frames']==state['framesStarted']
                    and state['frames']>=state['keys']+1):
                (self.directory/(name+'.json')).write_text(json.dumps(state, indent=2)+'\n')
                return state
            time.sleep(.2)
        (self.directory/'registers.txt').write_text(self.hmp('info registers'))
        raise AssertionError((name, state))

    def boot(self, restore, goal):
        state = self.wait('boot', lambda s:s['magic']==0x31545844 and s['frames']>=1)
        assert state['enabled']==1 and state['restore']==restore and state['goal']==PREFIX+goal, state
        assert state['keys']==0 and state['focus']==0, state
        return state

    def key(self, code):
        self.call('send-key', {'keys':[{'type':'qcode', 'data':code}], 'hold-time':60})
        # Avoid overlapping PS/2 presses; the guest owns state transitions.
        time.sleep(.12)

    def open(self):
        before = self.state()
        self.key('2')
        return self.wait('open', lambda s:s['focus']==2 and s['lastKey']==3 and s['frames']>before['frames'])

    def edit(self, code, goal):
        before = self.state()
        self.key(code)
        return self.wait('edit-'+code, lambda s:s['goal']==PREFIX+goal and s['keys']>before['keys'] and s['frames']>before['frames'])

    def commit(self, generation):
        before = self.state()
        self.key('f5')
        return self.wait('commit', lambda s:s['lastKey']==63 and s['commit']==generation and s['keys']>before['keys'] and s['frames']>before['frames'])

    def screenshot(self, name):
        assert self.hmp(f'screendump {name}.ppm') == ''
        spec = importlib.util.spec_from_file_location('desktop_observer', HERE/'check-native-desktop-qemu.py')
        observer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(observer)
        observer.ppm_png(self.directory/(name+'.ppm'), self.directory/(name+'.png'))

    def close(self, hard=False):
        if self.process.poll() is None:
            if hard:
                self.process.kill()
            else:
                self.call('quit')
            self.process.wait(timeout=10)
        if self.stream:
            self.stream.close()
        if self.sock:
            self.sock.close()
        self.log.close()


def main(args):
    for name in ('media', 'efi', 'kernel', 'qemu', 'firmware'):
        assert getattr(args, name).is_file(), (name, 'regular file required')
    args.output.mkdir(parents=True, exist_ok=False)
    before = pin(args.media)
    assert args.efi.read_bytes().count(args.kernel.read_bytes())==1
    spec = importlib.util.spec_from_file_location('media_reader', HERE/'check-native-media-format.py')
    reader = importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
    media_proof = reader.verify(args.media, args.efi)
    events = []
    active = None
    # Provision only an output-owned test fixture. The native guest refuses an
    # unmarked medium and never formats/provisions one itself.
    disk = args.output/'draft.raw'
    raw = bytearray(128*512)
    struct.pack_into('<4I', raw, 0, 0x49415741, 0x54465244, 1, 128)
    disk.write_bytes(raw)
    blank = bytes(raw)
    try:
        active = Guest(args, args.output/'commit', disk)
        active.boot(0, '')
        active.open()
        active.edit('a', 'a')
        active.edit('b', 'ab')
        committed = active.commit(1)
        assert committed['generation']==1
        active.screenshot('committed-ab')
        active.close();active=None
        first = disk.read_bytes()
        assert first[:512]==blank[:512] and first[2048:]==blank[2048:] and first!=blank
        assert hashlib.sha256(first[1536:2016]).digest()==first[2016:2048]
        events.append(dict(name='native-commit-generation-1', state=committed, disk=pin(disk)))

        active = Guest(args, args.output/'cold-restore', disk)
        restored = active.boot(1, 'ab')
        active.open();active.screenshot('restored-ab')
        active.edit('c', 'abc')
        active.close(hard=True);active=None
        assert disk.read_bytes()==first, 'uncommitted edit changed durable bytes'
        events.append(dict(name='cold-restore-and-uncommitted-power-loss', state=restored))

        active = Guest(args, args.output/'last-commit', disk)
        active.boot(1, 'ab');active.open();active.edit('d', 'abd')
        second_state = active.commit(2)
        active.close();active=None
        second = disk.read_bytes()
        assert second[:512]==blank[:512] and second[2048:]==blank[2048:]
        assert second[1536:2048]==first[1536:2048], 'previous committed slot overwritten'
        assert hashlib.sha256(second[1024:1504]).digest()==second[1504:1536]
        events.append(dict(name='native-commit-generation-2', state=second_state, disk=pin(disk)))

        # Even a fully hashed higher-generation orphan is not a published commit.
        orphan = bytearray(second)
        orphan[1536:2048] = second[1024:1536]
        struct.pack_into('<I', orphan, 1544, 3)
        orphan[2016:2048] = hashlib.sha256(orphan[1536:2016]).digest()
        disk.write_bytes(orphan)
        active = Guest(args, args.output/'orphan-refused', disk)
        restored = active.boot(2, 'abd')
        active.open();active.screenshot('restored-abd')
        active.close();active=None
        assert disk.read_bytes()==orphan
        events.append(dict(name='unpublished-higher-generation-ignored', state=restored))

        cases = []
        corrupt = bytearray(second);corrupt[1056] ^= 1
        cases.append(('tampered-slot', corrupt, -18))
        wrong = bytearray(second);struct.pack_into('<I', wrong, 516, 9)
        cases.append(('unsupported-head-version', wrong, -20))
        dirty = bytearray(blank);dirty[512+64] = 1
        cases.append(('dirty-empty-head', dirty, -16))
        reserved = bytearray(second);reserved[512+64] = 1
        cases.append(('head-reserved-bytes', reserved, -20))
        invalid_goal = bytearray(second);struct.pack_into('<I', invalid_goal, 1056, 0xD800)
        invalid_goal[1504:1536] = hashlib.sha256(invalid_goal[1024:1504]).digest()
        invalid_goal[992:1024] = invalid_goal[1504:1536]
        cases.append(('valid-digest-invalid-goal', invalid_goal, -17))
        unmarked = bytearray(blank);unmarked[0] ^= 1
        cases.append(('unprovisioned-medium', unmarked, -22))
        for name, data, result in cases:
            disk.write_bytes(data)
            active = Guest(args, args.output/name, disk)
            state = active.boot(result, '')
            active.open();active.commit(-24)
            active.close();active=None
            assert disk.read_bytes()==data, (name, 'refusal wrote disk')
            events.append(dict(name=name+'-refused-without-write', state=state))
        active = Guest(args, args.output/'missing-medium', None)
        state = active.boot(-22, '')
        active.open();active.commit(-24)
        active.close();active=None
        events.append(dict(name='missing-medium-refused', state=state))
        assert pin(args.media)==before
        report = dict(schema='aiueos.native-draft-restart.v1', status='PASS_NATIVE_LOCAL_DRAFT_COLD_RESTORE',
                      kernel=pin(args.kernel), efi=pin(args.efi), media=before, firmware=pin(args.firmware),
                      observer=pin(Path(__file__)), independentMedia=media_proof, events=events,
                      nativeATACommit=True, coldProcessBoot=True, previousCommitAfterUncommittedCrash=True,
                      PythonGuestImplementation=False, JVMNodeOrGraalFallback=False, cCompilerOrLoader=False,
                      BrowserCSSOMParity=False, registeredDappExecution=False, D2WorkspaceCodec=False,
                      signedHeadAuthority=False, releaseAdmission=False, distributedDurability=False,
                      physicalHardware=False)
        (args.output/'native-draft-restart.json').write_text(json.dumps(report, indent=2)+'\n')
        print(report['status'])
    finally:
        if active:
            active.close(hard=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('media', 'efi', 'kernel', 'qemu', 'firmware', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parsed = parser.parse_args()
    for name in vars(parsed):
        setattr(parsed, name, getattr(parsed, name).resolve())
    main(parsed)
