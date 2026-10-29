"""Source-level invariants for X4 Pro startup wake gate. Not hardware timing tests."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PowerWakeGuard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = (ROOT / 'src/main.cpp').read_text()
        cls.gpio = (ROOT / 'lib/hal/HalGPIO.cpp').read_text()
        cls.settings = (ROOT / 'src/CrossPointSettings.h').read_text()

    def test_wake_threshold_is_independent_constant(self):
        self.assertIn('getPowerWakeHoldDuration() { return 300; }', self.settings)
        self.assertIn('verifyPowerButtonWakeup(wakeGestureFromSettings(), CrossPointSettings::getPowerWakeHoldDuration())', self.main)

    def test_default_long_hold_wake_remains_enabled(self):
        start = self.main.index('static HalGPIO::WakeGestures wakeGestureFromSettings()')
        body = self.main[start:self.main.index('\n}', start)]
        self.assertIn('gestures.longHold = true;', body)

    def test_early_release_can_abort_wake(self):
        self.assertIn('WakeVerdict::ReleasedEarly', self.gpio)
        self.assertIn('WakeVerdict::NoSecondPress', self.gpio)

    def test_no_fixed_three_second_delay_in_wake_gate(self):
        start = self.gpio.index('constexpr unsigned long RELEASE_DEBOUNCE_MS = 30;')
        end = self.gpio.index('bool HalGPIO::isUsbHostLinkActive()', start)
        gate = self.gpio[start:end]
        self.assertNotIn('delay(3000)', gate)
        self.assertIn('DOUBLE_WINDOW_MS = 300;', gate)
        self.assertIn('POLL_INTERVAL_MS = 10;', gate)

if __name__ == '__main__':
    unittest.main()
