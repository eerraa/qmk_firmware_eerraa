"""Submodule metadata uses component-relative paths in either repository shape."""
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import patch

import qmk.submodules


class SubmoduleStatusTest(TestCase):
    def test_status_normalizes_git_root_paths(self):
        for prefix in ('', 'components/firmware/'):
            path = prefix + 'lib/chibios'

            def git_result(command, **kwargs):
                if command == ['git', 'rev-parse', '--show-prefix']:
                    output = prefix + '\n'
                elif command[:3] == ['git', 'config', '-f']:
                    output = 'submodule.lib/chibios.path=lib/chibios\n'
                elif command == ['git', 'submodule', 'status']:
                    output = ' abcdef0123456789 lib/chibios (release)\n'
                elif '--pretty=format:' in command[-1]:
                    output = f'{path}\x01abcdef0\x012026-11-07 00:00:00 +0000\x01Subject\n'
                else:
                    output = f'{path} release\n'
                return SimpleNamespace(stdout=output)

            with patch.object(qmk.submodules.cli, 'run', side_effect=git_result):
                self.assertEqual(qmk.submodules.status(), {
                    'lib/chibios': {
                        'name': 'lib/chibios',
                        'status': True,
                        'githash': 'abcdef0123456789',
                        'shorthash': 'abcdef0',
                        'last_log_timestamp': '2026-11-07 00:00:00 +0000',
                        'last_log_message': 'Subject',
                        'describe': 'release',
                    }
                })
