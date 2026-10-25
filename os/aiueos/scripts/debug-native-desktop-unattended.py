#!/usr/bin/env python3
"""Run a fresh Itonami Goal UI probe without VNC or manual guest input."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', type=Path,
                        help='one complete native desktop local-draft build')
    parser.add_argument('--goal', default='ab',
                        help='lowercase Goal body to enter and verify (default: ab)')
    parser.add_argument('--output', type=Path,
                        help='new private observation directory; defaults beside the build')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_dir():
        parser.error('build_dir must be an existing directory')
    if not re.fullmatch(r'[a-z ]{1,51}', args.goal) or not any(c != ' ' for c in args.goal):
        parser.error('--goal must contain 1-51 lowercase letters or spaces, including a letter')
    if args.output is None:
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        output = build.parent / f'{build.name}-qmp-debug-{stamp}-{os.getpid()}'
    else:
        output = args.output.resolve()
    if output.exists():
        parser.error('observation directory must not exist')
    keys = ['2'] + ['space' if c == ' ' else c for c in args.goal]
    command = [sys.executable,
               str(Path(__file__).with_name('debug-native-desktop-qmp.py')),
               '--build-dir', str(build), '--local-draft',
               '--output', str(output), '--keys', *keys,
               '--expect-focus', '2', '--expect-draft-enabled', '1',
               '--expect-generation', '0', '--expect-restore-code', '0',
               '--expect-commit-code', '0',
               '--expect-goal', 'Goal local:\n'+args.goal,
               '--expect-visible-goal', args.goal]
    result = subprocess.run(command, check=False)
    report_path = output/'debug.json'
    if report_path.is_file():
        report = json.loads(report_path.read_text())
        summary = {'status': report['status'], 'goal': args.goal,
                   'output': str(output), 'screenshot': str(output/'desktop.png'),
                   'receipt': str(report_path),
                   'vncUsed': report['vncUsed'],
                   'networkUsed': report['networkUsed']}
        print(json.dumps(summary, ensure_ascii=False))
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
