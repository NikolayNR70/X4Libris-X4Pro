"""Regression guard: configurable reader timing must not change Power wake."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class PowerTimingIsolation(unittest.TestCase):
    def test_reader_hold_and_double_exclude_power(self):
        source = (ROOT / 'src/ButtonEventManager.cpp').read_text()
        self.assertRegex(source, r'longPressMs\(Button button\)[^}]*button != Button::Power')
        self.assertRegex(source, r'doubleWindowMs\(Button button\)[^}]*button != Button::Power')

    def test_wake_gate_has_separate_fixed_threshold(self):
        settings = (ROOT / 'src/CrossPointSettings.h').read_text()
        self.assertIn('getPowerWakeHoldDuration() { return 300; }', settings)
        main = (ROOT / 'src/main.cpp').read_text()
        self.assertIn('gpio.verifyPowerButtonWakeup(wakeGestureFromSettings(), CrossPointSettings::getPowerWakeHoldDuration())', main)

    def test_boot_release_is_serviced_nonblocking(self):
        main = (ROOT / 'src/main.cpp').read_text()
        start = main.index('static void serviceBootPowerRelease()')
        end = main.index('\n}', start)
        fn = main[start:end]
        self.assertIn('buttonEventManager.drain()', fn)
        self.assertNotIn('delay(', fn)
        self.assertIn('serviceBootPowerRelease();', main[end:])

if __name__ == '__main__':
    unittest.main()
