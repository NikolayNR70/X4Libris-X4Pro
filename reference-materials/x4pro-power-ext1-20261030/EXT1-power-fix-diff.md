# EXT1 Power-wake source change

Reference-only change to `baseline-source-v3.1.3/lib/hal/HalGPIO.cpp`, in `HalGPIO::getWakeupReason()`:

```diff
-      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP)) {
+      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP) ||
+      (wakeupCause == ESP_SLEEP_WAKEUP_EXT1 && resetReason == ESP_RST_DEEPSLEEP)) {
```

The resulting candidate source file is preserved at `candidate-diff/lib/hal/HalGPIO.cpp`. To recreate the source candidate, apply the displayed condition to the baseline file; do not apply this historical change to the active v1.0 branch without a new explicit task.
