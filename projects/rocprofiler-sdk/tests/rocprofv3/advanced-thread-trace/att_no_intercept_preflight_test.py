"""CPU-only regression coverage for the ATT decoder preflight."""

import contextlib
import ctypes
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

import att_no_intercept_preflight as preflight


def validator_fixture():
    spec = importlib.util.spec_from_file_location(
        "att_preflight_fixtures", Path(__file__).with_name("conftest.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validator_request(output_path, library="decoder.so"):
    options = {
        "--att-no-intercept-out-dir": str(output_path),
        "--att-decoder-library": library,
    }
    return SimpleNamespace(config=SimpleNamespace(getoption=options.get))


def test_unsupported_validator_skips_stale_output(tmp_path):
    import pytest

    fixtures = validator_fixture()
    (tmp_path / "out_results.json").write_text("stale output")
    with mock.patch.object(fixtures, "quick_scan_supported", return_value=False):
        with pytest.raises(pytest.skip.Exception, match=preflight.SKIP_REASON):
            fixtures.att_no_intercept_out_dir_path.__wrapped__(
                validator_request(tmp_path)
            )


def test_supported_validator_preserves_output_path(tmp_path):
    fixtures = validator_fixture()
    with mock.patch.object(fixtures, "quick_scan_supported", return_value=True) as probe:
        assert fixtures.att_no_intercept_out_dir_path.__wrapped__(
            validator_request(tmp_path)
        ) == str(tmp_path)
        probe.assert_called_once_with("decoder.so")
        assert fixtures.att_no_intercept_out_dir_path.__wrapped__(
            validator_request(tmp_path, library=None)
        ) == str(tmp_path)
        probe.assert_called_once_with("decoder.so")


def test_validator_decoder_failure_is_not_a_skip(tmp_path):
    import pytest

    fixtures = validator_fixture()
    with mock.patch.object(
        fixtures, "quick_scan_supported", side_effect=RuntimeError("decoder error")
    ):
        with pytest.raises(RuntimeError, match="decoder error"):
            fixtures.att_no_intercept_out_dir_path.__wrapped__(
                validator_request(tmp_path)
            )


def test_failure_traceback_does_not_match_skip_regex(tmp_path):
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "pytest",
            str(Path(__file__).with_name("validate.py"))
            + "::test_att_no_intercept_target_kernel",
            "--att-no-intercept-out-dir",
            str(tmp_path),
            "--att-decoder-library",
            str(tmp_path / "missing-decoder.so"),
            "--confcutdir=" + str(Path(__file__).parent),
            "--tb=long",
            "-q",
            "-o",
            "addopts=",
            "-p",
            "no:cacheprovider",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    assert result.returncode == 1, result.stdout
    assert "OSError" in result.stdout
    assert preflight.SKIP_REASON.split(":", 1)[0] not in result.stdout


class AttNoInterceptPreflightTest(unittest.TestCase):
    def setUp(self):
        self.decoder = mock.Mock()
        self.decoder.rocprof_trace_decoder_create_handle.return_value = 0
        self.decoder.rocprof_trace_decoder_destroy_handle.return_value = 0
        self.decoder.rocprof_trace_decoder_quick_scan.return_value = 0
        self.decoder.rocprof_trace_decoder_get_status_string.return_value = (
            b"decoder error"
        )
        self.loader = mock.patch.object(
            preflight.ctypes, "CDLL", return_value=self.decoder
        )
        self.loader.start()
        self.addCleanup(self.loader.stop)

    def test_supported_probe_releases_handle(self):
        self.assertTrue(preflight.quick_scan_supported("decoder.so"))
        self.decoder.rocprof_trace_decoder_create_handle.assert_called_once()
        self.decoder.rocprof_trace_decoder_destroy_handle.assert_called_once()
        args = self.decoder.rocprof_trace_decoder_quick_scan.call_args[0]
        self.assertIsInstance(args[0], preflight.DecoderHandle)
        self.assertEqual(args[1:], (0, None, 0, None, None))
        self.assertEqual(ctypes.sizeof(preflight.DecoderHandle), 8)

    def test_unimplemented_probe_releases_handle(self):
        self.decoder.rocprof_trace_decoder_quick_scan.return_value = 71
        self.decoder.rocprof_trace_decoder_get_status_string.return_value = (
            b"ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR_NOT_IMPLEMENTED"
        )
        self.assertFalse(preflight.quick_scan_supported("decoder.so"))
        self.decoder.rocprof_trace_decoder_destroy_handle.assert_called_once()

    def test_create_failure_is_not_a_skip(self):
        self.decoder.rocprof_trace_decoder_create_handle.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "decoder error"):
            preflight.quick_scan_supported("decoder.so")
        self.decoder.rocprof_trace_decoder_destroy_handle.assert_not_called()
        self.decoder.rocprof_trace_decoder_quick_scan.assert_not_called()

    def test_probe_failure_is_not_a_skip(self):
        self.decoder.rocprof_trace_decoder_quick_scan.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "decoder error"):
            preflight.quick_scan_supported("decoder.so")
        self.decoder.rocprof_trace_decoder_destroy_handle.assert_called_once()

    def test_destroy_failure_is_not_a_skip(self):
        self.decoder.rocprof_trace_decoder_destroy_handle.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "decoder error"):
            preflight.quick_scan_supported("decoder.so")

    def test_loader_failure_is_not_a_skip(self):
        with mock.patch.object(
            preflight.ctypes, "CDLL", side_effect=OSError("load error")
        ):
            with self.assertRaisesRegex(OSError, "load error"):
                preflight.quick_scan_supported("missing.so")

    def test_missing_api_is_not_a_skip(self):
        del self.decoder.rocprof_trace_decoder_quick_scan
        with self.assertRaises(AttributeError):
            preflight.quick_scan_supported("decoder.so")

    def test_supported_capture_preserves_arguments(self):
        with mock.patch.object(preflight.os, "execvp") as execute:
            preflight.main(
                ["--library", "decoder.so", "--", "rocprofv3", "--", "a workload"]
            )
        execute.assert_called_once_with("rocprofv3", ["rocprofv3", "--", "a workload"])

    def test_unsupported_capture_is_not_launched(self):
        self.decoder.rocprof_trace_decoder_quick_scan.return_value = 71
        self.decoder.rocprof_trace_decoder_get_status_string.return_value = (
            b"ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR_NOT_IMPLEMENTED"
        )
        output = io.StringIO()
        with mock.patch.object(preflight.os, "execvp") as execute:
            with contextlib.redirect_stdout(output):
                self.assertEqual(
                    preflight.main(["--library", "decoder.so", "--", "rocprofv3"]),
                    0,
                )
        execute.assert_not_called()
        self.assertEqual(output.getvalue().strip(), preflight.SKIP_REASON)

    def test_capture_requires_command(self):
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                preflight.main(["--library", "decoder.so"])
        self.assertEqual(error.exception.code, 2)
        self.decoder.rocprof_trace_decoder_create_handle.assert_not_called()


if __name__ == "__main__":
    unittest.main()
