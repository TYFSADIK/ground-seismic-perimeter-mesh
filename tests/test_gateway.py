import os
import shutil
import subprocess
import tempfile
import unittest

from gateway.decode import SIZE, Deduper, decode
from gateway.tune import tune

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class TestGateway(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++"), "needs a C++ compiler")
    def test_cross_language_packet(self):
        """Firmware C++ signs a packet; Python gateway must decode and verify it."""
        exe = os.path.join(tempfile.mkdtemp(), "t")
        subprocess.check_call(["c++", "-std=c++17", "-O1", "-Ifirmware/lib/seismic", "firmware/test/test_seismic.cpp", "-o", exe], cwd=HERE)
        raw = bytes.fromhex(subprocess.check_output([exe, "--emit"]).decode().strip())
        self.assertEqual(len(raw), SIZE)
        d = decode(raw, bytes([1, 2, 3] + [0] * 29))
        self.assertTrue(d.authentic)
        self.assertEqual((d.src, d.seq, d.label), (7, 42, "vehicle"))
        self.assertFalse(decode(raw, b"wrong key" + bytes(23)).authentic)
        dd = Deduper()
        self.assertTrue(dd.fresh(d))
        self.assertFalse(dd.fresh(d))

    def test_tune(self):
        rows = [{"label": "vehicle", "rms": 0.01, "low": 0.8, "mid": 0.1, "high": 0.1, "kurtosis": 3, "cadence": 0.1},
                {"label": "footsteps", "rms": 0.01, "low": 0.2, "mid": 0.7, "high": 0.1, "kurtosis": 8, "cadence": 0.7},
                {"label": "quiet", "rms": 0.0005, "low": 0.3, "mid": 0.3, "high": 0.4, "kurtosis": 3, "cadence": 0.0}]
        th, s = tune(rows)
        self.assertEqual(s, 1.0)


if __name__ == "__main__":
    unittest.main()
