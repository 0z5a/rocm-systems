#!/usr/bin/env python3
# Copyright Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

"""Guard the permissions the maintainer scripts leave on the amd-smi log path.

The log directory must be root-owned and not writable by other users, on a
fresh install and on an upgrade over a directory left loose by an older
package. do_configureLogrotate() is run for real under bash against a temp
directory, so the tests need no root, package manager or container.
"""

import os
import re
import shlex
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

# .../unit/system/<this file> -> amdsmi source root, or share/amd_smi when installed.
REPO_ROOT = Path(__file__).resolve().parents[4]

SCRIPTLETS = {"deb": "DEBIAN/postinst.in", "rpm": "RPM/post.in"}

# dpkg honors the #!/bin/bash line; rpm runs the scriptlet text with /bin/sh.
SHELLS = {"deb": "bash", "rpm": "sh"}

LOG_DIR_LITERAL = "/var/log/amd_smi_lib"
LOGROTATE_CONF_LITERAL = "/etc/logrotate.d/amd_smi.conf"

LOG_FILE_NAME = "AMD-SMI-lib.log"

LOG_DIR_MODE = 0o755
LOG_FILE_MODE = 0o644
VICTIM_MODE = 0o600


FUNCTION_START = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*\(\) \{", re.M)


def _configure_function(relative_path: str) -> str:
    # The function embeds a heredoc with its own column-0 "}", so slice up to
    # the next top-level function instead of matching the closing brace.
    path = REPO_ROOT / relative_path
    if not path.is_file():
        raise unittest.SkipTest("{} not found".format(relative_path))
    script = path.read_text(encoding="utf-8")
    starts = list(FUNCTION_START.finditer(script))
    for index, match in enumerate(starts[:-1]):
        if script.startswith("do_configureLogrotate", match.start()):
            return script[match.start() : starts[index + 1].start()]
    raise AssertionError("do_configureLogrotate() not followed by a function in " + relative_path)


def _run_configure(function_text: str, log_dir: Path, scratch: Path, shell: str) -> list:
    """Run the function with its literal paths pointed into a temp tree.

    The paths are re-quoted so a directory name containing a space exercises
    the function's own quoting. chown is stubbed because the test is not root;
    the arguments of each call are returned, one string per call.
    """
    conf = scratch / "amd_smi.conf"
    chown_calls = scratch / "chown.calls"
    bindir = scratch / "bin"
    bindir.mkdir()
    chown = bindir / "chown"
    chown.write_text(
        "#!/bin/sh\nprintf '%s\\n' \"$*\" >> {}\n".format(shlex.quote(str(chown_calls))),
        encoding="utf-8",
    )
    chown.chmod(0o755)

    text = function_text.replace(
        "logPath=" + LOG_DIR_LITERAL, "logPath=" + shlex.quote(str(log_dir))
    ).replace(
        "logrotateConfFile=" + LOGROTATE_CONF_LITERAL, "logrotateConfFile=" + shlex.quote(str(conf))
    )
    if LOG_DIR_LITERAL in text or LOGROTATE_CONF_LITERAL in text:
        raise AssertionError("a system path survived substitution; refusing to run")

    env = {"PATH": "{}:/usr/bin:/bin".format(bindir)}
    subprocess.run(
        [shell, "-c", text + "\ndo_configureLogrotate"],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=True,
    )
    if not chown_calls.is_file():
        return []
    return chown_calls.read_text(encoding="utf-8").splitlines()


def _mode(path: Path) -> int:
    return stat.S_IMODE(path.lstat().st_mode)


class LogDirPermissionsTest(unittest.TestCase):
    def _fresh_tree(self) -> Path:
        tmp = tempfile.TemporaryDirectory(prefix="amd smi ")
        self.addCleanup(tmp.cleanup)
        return Path(tmp.name)

    def test_upgrade_repairs_world_writable_log_dir(self):
        for kind, relative_path in SCRIPTLETS.items():
            with self.subTest(scriptlet=kind):
                scratch = self._fresh_tree()
                log_dir = scratch / "amd_smi_lib"
                log_dir.mkdir()
                log_dir.chmod(0o777)

                _run_configure(_configure_function(relative_path), log_dir, scratch, SHELLS[kind])

                self.assertEqual(oct(_mode(log_dir)), oct(LOG_DIR_MODE))

    def test_log_file_is_not_writable_by_group_or_others(self):
        for kind, relative_path in SCRIPTLETS.items():
            with self.subTest(scriptlet=kind):
                scratch = self._fresh_tree()
                log_dir = scratch / "amd_smi_lib"

                _run_configure(_configure_function(relative_path), log_dir, scratch, SHELLS[kind])

                self.assertEqual(oct(_mode(log_dir / LOG_FILE_NAME)), oct(LOG_FILE_MODE))

    def test_log_dir_and_file_are_chowned_to_root(self):
        for kind, relative_path in SCRIPTLETS.items():
            with self.subTest(scriptlet=kind):
                scratch = self._fresh_tree()
                log_dir = scratch / "amd_smi_lib"

                calls = _run_configure(
                    _configure_function(relative_path), log_dir, scratch, SHELLS[kind]
                )

                for target in (log_dir, log_dir / LOG_FILE_NAME):
                    self.assertIn("root:root -- {}".format(target), calls)

    def test_planted_symlink_is_replaced_and_target_left_alone(self):
        for kind, relative_path in SCRIPTLETS.items():
            with self.subTest(scriptlet=kind):
                scratch = self._fresh_tree()
                log_dir = scratch / "amd_smi_lib"
                log_dir.mkdir()
                victim = scratch / "victim"
                victim.write_text("secret\n", encoding="utf-8")
                victim.chmod(VICTIM_MODE)
                (log_dir / LOG_FILE_NAME).symlink_to(victim)

                _run_configure(_configure_function(relative_path), log_dir, scratch, SHELLS[kind])

                self.assertEqual(oct(_mode(victim)), oct(VICTIM_MODE))
                self.assertEqual(victim.read_text(encoding="utf-8"), "secret\n")
                self.assertFalse((log_dir / LOG_FILE_NAME).is_symlink())

    def test_hard_linked_log_is_replaced_and_other_link_left_alone(self):
        for kind, relative_path in SCRIPTLETS.items():
            with self.subTest(scriptlet=kind):
                scratch = self._fresh_tree()
                log_dir = scratch / "amd_smi_lib"
                log_dir.mkdir()
                victim = scratch / "victim"
                victim.write_text("secret\n", encoding="utf-8")
                victim.chmod(VICTIM_MODE)
                os.link(str(victim), str(log_dir / LOG_FILE_NAME))

                _run_configure(_configure_function(relative_path), log_dir, scratch, SHELLS[kind])

                self.assertEqual(oct(_mode(victim)), oct(VICTIM_MODE))
                self.assertEqual(victim.read_text(encoding="utf-8"), "secret\n")
                self.assertEqual((log_dir / LOG_FILE_NAME).stat().st_nlink, 1)
