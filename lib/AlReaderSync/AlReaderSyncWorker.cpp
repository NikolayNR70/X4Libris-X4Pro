#include "AlReaderSyncWorker.h"

#include <Arduino.h>
#include <HalClock.h>
#include <Logging.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <utility>

#include "../../src/CrossPointSettings.h"
#include "../../src/WifiCredentialStore.h"

namespace {
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr unsigned long kWifiConnectTimeoutMs = 10000;
constexpr unsigned long kIdleExitMs = 30000;

SemaphoreHandle_t mutex = nullptr;
TaskHandle_t task = nullptr;
AlReaderSync::Job slot;
bool occupied = false;
bool done = false;
uint64_t activeSequence = 0;
uint64_t nextSequence = 1;
std::atomic<bool> busySnapshot{false};

class Lock {
 public:
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

bool ensureMutex() {
  if (mutex == nullptr) mutex = xSemaphoreCreateMutex();
  return mutex != nullptr;
}

void publishBusy() { busySnapshot.store(occupied && !done, std::memory_order_relaxed); }

bool connectToSavedWifi() {
  // Do not take over an AP/captive-portal radio mode. This worker only owns
  // station-mode connections and never tears down Wi-Fi it did not start.
  if (WiFi.getMode() != WIFI_MODE_NULL && WiFi.getMode() != WIFI_MODE_STA) return false;
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    return true;
  }

  const auto& store = WIFI_STORE;
  const std::string last = store.getLastConnectedSsid();
  const WifiCredential* credential = last.empty() ? nullptr : store.findCredential(last);
  if (credential == nullptr && !store.getCredentials().empty()) credential = &store.getCredentials().front();
  if (credential == nullptr) return false;

  const std::string ssid = credential->ssid;
  const std::string password = credential->password;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kWifiConnectTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false);
    return false;
  }
  WiFi.setSleep(false);
  return true;
}

bool mayTearDownRadio() { return WiFi.getMode() == WIFI_MODE_STA; }

void perform(AlReaderSync::Job& job) {
  const bool startedRadio = WiFi.getMode() == WIFI_MODE_NULL;
  if (!connectToSavedWifi()) {
    job.result.error = AlReaderSync::Error::Transport;
    job.result.message = "Could not connect to a saved Wi-Fi network";
    if (startedRadio && mayTearDownRadio()) WiFi.mode(WIFI_OFF);
    return;
  }

  // NTP/TLS setup can take time too, so it belongs on this task. The HTTP client
  // enforces its own connect and total deadlines and never retries indefinitely.
  HalClock::ensureUsableForTls(SETTINGS.ntpServer);
  switch (job.operation) {
    case AlReaderSync::Job::Operation::FETCH:
      job.result = AlReaderSync::AlReaderWebDavClient::fetch(job.webDavUrl, job.username, job.password, job.bookKey);
      break;
    case AlReaderSync::Job::Operation::UPDATE:
      job.result = AlReaderSync::AlReaderWebDavClient::update(job.webDavUrl, job.username, job.password,
                                                              job.bookKey, job.record);
      break;
  }

  if (startedRadio && mayTearDownRadio()) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
}

void workerTask(void*) {
  for (;;) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kIdleExitMs)) == 0) {
      bool exit = false;
      {
        const Lock lock;
        if (!occupied || done) {
          task = nullptr;
          exit = true;
        }
      }
      if (exit) vTaskDelete(nullptr);
      continue;
    }

    AlReaderSync::Job current;
    {
      const Lock lock;
      if (!occupied || done) continue;
      current = std::move(slot);
    }
    perform(current);
    {
      const Lock lock;
      slot = std::move(current);
      done = true;
      publishBusy();
    }
  }
}

bool ensureTask() {
  if (task != nullptr) return true;
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t core = 1;
#else
  constexpr BaseType_t core = 0;
#endif
  return xTaskCreatePinnedToCore(workerTask, "AlReaderSync", kWorkerStackBytes, nullptr, 1, &task, core) == pdPASS;
}
}  // namespace

namespace AlReaderSync::Worker {
bool isBusy() { return busySnapshot.load(std::memory_order_relaxed); }

bool post(Job&& job, uint64_t* sequenceOut) {
  if (!ensureMutex()) return false;
  TaskHandle_t notifyTask = nullptr;
  uint64_t sequence = 0;
  {
    const Lock lock;
    if (occupied && !done) return false;
    if (!ensureTask()) return false;
    slot = std::move(job);
    occupied = true;
    done = false;
    activeSequence = nextSequence++;
    sequence = activeSequence;
    notifyTask = task;
    publishBusy();
  }
  xTaskNotifyGive(notifyTask);
  if (sequenceOut) *sequenceOut = sequence;
  return true;
}

bool isDone(const uint64_t sequence) {
  if (!ensureMutex()) return true;
  const Lock lock;
  return !(occupied && activeSequence == sequence && !done);
}

Job consume(const uint64_t sequence) {
  Job out;
  out.result.error = Error::Transport;
  out.result.message = "No completed AlReader sync result";
  if (!ensureMutex()) return out;
  const Lock lock;
  if (occupied && activeSequence == sequence && done) {
    out = std::move(slot);
    slot = Job{};
    occupied = false;
    publishBusy();
  }
  return out;
}

bool drain(const unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (isBusy()) {
    if (millis() - start >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return true;
}
}  // namespace AlReaderSync::Worker
