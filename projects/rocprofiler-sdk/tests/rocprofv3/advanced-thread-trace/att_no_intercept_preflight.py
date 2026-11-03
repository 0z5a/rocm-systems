"""Check the decoder's runtime quick-scan capability before an ATT capture."""

import argparse
import ctypes
import os

SKIP_REASON = "SKIP_ATT_NO_INTERCEPT_QUICK_SCAN: decoder quick-scan is unavailable"


class DecoderHandle(ctypes.Structure):
    _fields_ = [("handle", ctypes.c_uint64)]


def quick_scan_supported(library):
    decoder = ctypes.CDLL(library)
    decoder.rocprof_trace_decoder_create_handle.argtypes = [ctypes.POINTER(DecoderHandle)]
    decoder.rocprof_trace_decoder_create_handle.restype = ctypes.c_int
    decoder.rocprof_trace_decoder_destroy_handle.argtypes = [DecoderHandle]
    decoder.rocprof_trace_decoder_destroy_handle.restype = ctypes.c_int
    decoder.rocprof_trace_decoder_quick_scan.argtypes = [
        DecoderHandle,
        ctypes.c_uint64,
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_void_p,
        ctypes.c_void_p,
    ]
    decoder.rocprof_trace_decoder_quick_scan.restype = ctypes.c_int
    decoder.rocprof_trace_decoder_get_status_string.argtypes = [ctypes.c_int]
    decoder.rocprof_trace_decoder_get_status_string.restype = ctypes.c_char_p

    def check_status(status):
        if status != 0:
            raise RuntimeError(
                decoder.rocprof_trace_decoder_get_status_string(status).decode("utf-8")
            )

    handle = DecoderHandle()
    check_status(decoder.rocprof_trace_decoder_create_handle(ctypes.byref(handle)))
    try:
        status = decoder.rocprof_trace_decoder_quick_scan(handle, 0, None, 0, None, None)
        if status != 0:
            name = decoder.rocprof_trace_decoder_get_status_string(status).decode("utf-8")
            if name == "ROCPROFILER_THREAD_TRACE_DECODER_STATUS_ERROR_NOT_IMPLEMENTED":
                return False
            check_status(status)
        return True
    finally:
        check_status(decoder.rocprof_trace_decoder_destroy_handle(handle))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a capture command is required")
    if not quick_scan_supported(args.library):
        print(SKIP_REASON, flush=True)
        return 0
    os.execvp(command[0], command)


if __name__ == "__main__":
    raise SystemExit(main())
