"""Guard observed Witchhunt early-boot wake gate behavior without changing power code."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class WakeGateStructure(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gpio = (ROOT / "lib/hal/HalGPIO.cpp").read_text()
        cls.main = (ROOT / "src/main.cpp").read_text()

    def test_wake_gate_runs_before_other_boot_initialization(self):
        self.assertLess(self.main.index("gpio.verifyPowerButtonWakeup("),
                        self.main.index("BootDiag::markPhase(BootPhase::WakeGate)"))

    def test_long_hold_is_always_enabled_in_witchhunt(self):
        fn = self.main.split("static HalGPIO::WakeGestures wakeGestureFromSettings()", 1)[1].split("\n}", 1)[0]
        self.assertIn("gestures.longHold = true;", fn)

    def test_gate_polls_for_hold_or_release(self):
        fn = self.gpio.split("HalGPIO::WakeCheck HalGPIO::verifyPowerButtonWakeup(", 1)[1].split("\n}", 1)[0]
        self.assertIn("while (true)", fn)
        self.assertIn("delay(POLL_INTERVAL_MS)", fn)
        self.assertIn("WakeVerdict::LongHold", fn)
        self.assertIn("WakeVerdict::ReleasedEarly", fn)

    def test_gate_has_no_fixed_three_second_delay(self):
        fn = self.gpio.split("HalGPIO::WakeCheck HalGPIO::verifyPowerButtonWakeup(", 1)[1].split("\n}", 1)[0]
        self.assertNotRegex(fn, r"\bdelay\s*\(\s*3000\s*\)")
        self.assertIn("constexpr unsigned long DOUBLE_WINDOW_MS = 300;", fn)
        self.assertIn("constexpr unsigned long BOUNCE_TOLERANCE_MS = 100;", fn)

if __name__ == "__main__":
    unittest.main()
