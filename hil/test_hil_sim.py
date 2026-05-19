"""Tests for the HIL simulator packet format."""
import struct
import pytest
from hil.hil_sim import make_frame, FRAME_FMT, FRAME_SIZE, MAGIC


def test_frame_size():
    payload = make_frame(1, 0.0)
    assert len(payload) == FRAME_SIZE == 120


def test_magic_correct():
    payload = make_frame(1, 0.0)
    magic = struct.unpack_from("<I", payload, 0)[0]
    assert magic == MAGIC


def test_sequence_encoded():
    payload = make_frame(42, 1.0)
    seq = struct.unpack_from("<I", payload, 4)[0]
    assert seq == 42


def test_timestamp_nonzero():
    payload = make_frame(1, 1.5)
    ts_ns = struct.unpack_from("<Q", payload, 8)[0]
    assert ts_ns > 0


def test_outlier_fault_inflates_pos():
    normal  = make_frame(1, 1.0, fault=None)
    outlier = make_frame(1, 1.0, fault="outlier")
    x_normal  = struct.unpack_from("<d", normal,  16)[0]
    x_outlier = struct.unpack_from("<d", outlier, 16)[0]
    assert abs(x_outlier) > abs(x_normal) * 100
