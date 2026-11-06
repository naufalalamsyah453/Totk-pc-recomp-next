"""Build and run tests/opt_msgkey_check.c: runtime/ui/opt_msgkey.h's classification of the Options screen's message keys.

Needs no game files.

Usage: opt_msgkey_check.py
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests' / 'diff'))
from common import MSYS_BIN, compiler  # noqa: E402


def main():
    work = ROOT / 'build' / 'test' / 'opt_msgkey'
    work.mkdir(parents=True, exist_ok=True)
    exe = work / ('opt_msgkey_check.exe' if os.name == 'nt' else 'opt_msgkey_check')
    env = dict(os.environ)
    if os.name == 'nt' and MSYS_BIN.exists():
        env['PATH'] = str(MSYS_BIN) + os.pathsep + env.get('PATH', '')
    subprocess.check_call([compiler(), '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'runtime'),
                           str(ROOT / 'tests' / 'opt_msgkey_check.c'), '-o', str(exe)], env=env)
    return subprocess.run([str(exe)], env=env).returncode


if __name__ == '__main__':
    sys.exit(main())
