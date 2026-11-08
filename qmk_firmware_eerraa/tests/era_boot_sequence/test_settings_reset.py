#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute install-reset consumption and bounded erasure against NOR adapters."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_boot_sequence import ROOT, HERE


class SettingsReset(unittest.TestCase):
    def test_install_reset_preserves_settings_until_reinstall(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / 'settings_reset'
            compiled = subprocess.run([
                'gcc', '-std=c11', '-O2', '-Werror=implicit-function-declaration',
                '-I' + str(ROOT),
                str(ROOT / 'keyboards/era/common/storage/era_settings_reset.c'),
                str(HERE / 'settings_reset.c'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
