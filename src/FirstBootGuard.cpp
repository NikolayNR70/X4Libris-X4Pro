#include "FirstBootGuard.h"
#include "BootTrialHealth.h"
#include <Logging.h>
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <esp_attr.h>
#include <esp_private/esp_clk.h>
#include <nvs.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <memory>
#include "network/OtaBootSwitch.h"

namespace FirstBootGuard {
#if FREEINK_DEVICE_X4PRO
namespace {
constexpr uint32_t MAGIC = 0x31474246;
constexpr uint32_t RESET_WINDOW_MAGIC = 0x58475231;
constexpr uint64_t RESET_WINDOW_US = 180ULL * 1000000ULL;
struct Record { uint32_t magic; uint32_t fallback; uint8_t accepted[32]; uint8_t trial[32]; uint32_t pending; };
struct ResetWindowStamp { uint32_t magic; uint64_t startedAtUs; uint32_t checksum; };
RTC_NOINIT_ATTR ResetWindowStamp resetWindowStamp;
Record record{};
nvs_handle_t prefs = 0;
const esp_partition_t* previous = nullptr;
std::atomic<bool> probation{false};
BootTrialHealth health;
esp_timer_handle_t timer = nullptr;
uint8_t runningHash[32]{};
uint32_t resetWindowChecksum(uint64_t startedAtUs) {
  return RESET_WINDOW_MAGIC ^ static_cast<uint32_t>(startedAtUs) ^
         static_cast<uint32_t>(startedAtUs >> 32) ^ 0xA53C91E7;
}
bool resetWindowStampValid() {
  return resetWindowStamp.magic == RESET_WINDOW_MAGIC &&
         resetWindowStamp.checksum == resetWindowChecksum(resetWindowStamp.startedAtUs);
}
void startResetWindow() {
  resetWindowStamp.magic = 0;
  resetWindowStamp.startedAtUs = esp_clk_rtc_time();
  resetWindowStamp.checksum = resetWindowChecksum(resetWindowStamp.startedAtUs);
  resetWindowStamp.magic = RESET_WINDOW_MAGIC;
}
bool resetWindowExpired() {
  if (!resetWindowStampValid()) return false;
  const uint64_t now = esp_clk_rtc_time();
  return now >= resetWindowStamp.startedAtUs && now - resetWindowStamp.startedAtUs >= RESET_WINDOW_US;
}
void clearResetGrace() {
  if (prefs) {
    nvs_erase_key(prefs, "reset_grace");
    nvs_commit(prefs);
  }
}
bool resetGraceRequiresRollback() {
  uint8_t enabled = 0;
  if (!prefs || nvs_get_u8(prefs, "reset_grace", &enabled) != ESP_OK || !enabled) return false;
  if (resetWindowExpired()) {
    clearResetGrace();
    return false;
  }
  // Deep-sleep wake is the normal power-button path on X4 Pro. Any other
  // restart during the grace window is treated as a failed run.
  return esp_reset_reason() != ESP_RST_DEEPSLEEP;
}
// Verify the actual bytes, not only an app descriptor. The firmware's patched
// image header is rejected by esp_image_verify on some OEM bootloaders.
bool verify(const esp_partition_t* p, uint8_t digest[32]) {
  if (!p || p->size < 65536) return false;
  uint8_t h[24];
  if (esp_partition_read(p, 0, h, sizeof h) != ESP_OK || h[0] != 0xe9 || h[1] == 0 || h[1] > 16 ||
      h[12] != 9 || h[13] != 0 || h[23] != 1) return false;
  uint32_t end = 24;
  for (unsigned i=0; i<h[1]; ++i) {
    uint32_t seg[2];
    if (end > p->size-8 || esp_partition_read(p,end,seg,8)!=ESP_OK) return false;
    end += 8;
    if (seg[1] > p->size-end) return false;
    end += seg[1];
  }
  if (end > p->size-48) return false;
  const uint32_t hashed = (end+16)&~15u;
  auto buffer = std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[4096]);
  if (!buffer) return false;
  // The ESP-IDF mbedTLS port uses the S3 SHA accelerator. The previous
  // software-only WolfSSL digest hashed the entire application before any
  // visible startup feedback, on every wake. Keep full-byte verification.
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts(&sha,0)!=0) {mbedtls_sha256_free(&sha);return false;}
  bool ok=true;
  for (uint32_t offset=0; offset<hashed;) {
    const uint32_t n=std::min<uint32_t>(4096,hashed-offset);
    if (esp_partition_read(p,offset,buffer.get(),n)!=ESP_OK || mbedtls_sha256_update(&sha,buffer.get(),n)!=0) {ok=false;break;}
    offset+=n;
  }
  uint8_t actual[32];
  if (ok) ok=mbedtls_sha256_finish(&sha,actual)==0 && esp_partition_read(p,hashed,digest,32)==ESP_OK &&
             std::memcmp(actual,digest,32)==0;
  mbedtls_sha256_free(&sha);
  return ok;
}
bool save() {
  return prefs && nvs_set_blob(prefs,"record",&record,sizeof record)==ESP_OK && nvs_commit(prefs)==ESP_OK;
}
const esp_partition_t* at(uint32_t address) {
  auto it=esp_partition_find(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_ANY,nullptr);
  while (it) {
    auto p=esp_partition_get(it);
    if(p->address==address) {esp_partition_iterator_release(it);return p;}
    it=esp_partition_next(it);
  }
  return nullptr;
}
// OTA sequence changes on an explicit installation, but not on a reset of the
// same trial. Store it in a separate NVS key to keep the record readable by .3/.4.
uint32_t runningSequence(const esp_partition_t* running) {
  auto data=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr);
  if(!data || data->size<8192) return 0;
  uint32_t sequence=0;
  for(unsigned i=0;i<2;++i) {
    ota_boot::SelectEntry e{};
    if(esp_partition_read(data,i*4096,&e,sizeof e)!=ESP_OK) return 0;
    if(!e.ota_seq || e.ota_seq==UINT32_MAX || e.crc!=ota_boot::computeSeqCrc(e.ota_seq) ||
       e.ota_state==ota_boot::kOtaImgInvalid || e.ota_state==ota_boot::kOtaImgAborted) continue;
    auto p=esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0+(e.ota_seq-1)%2),nullptr);
    if(p && p->address==running->address && e.ota_seq>sequence) sequence=e.ota_seq;
  }
  return sequence;
}
const esp_partition_t* priorSlot(const esp_partition_t* running) {
  auto data=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr);
  if(!data || data->size<8192) return nullptr;
  ota_boot::SelectEntry entry[2];
  if(esp_partition_read(data,0,&entry[0],32)!=ESP_OK || esp_partition_read(data,4096,&entry[1],32)!=ESP_OK) return nullptr;
  // Only an OTA slot referenced as VALID is a recovery candidate. Do not guess
  // from stale bytes in the other app partition (it may hold a different board).
  for(auto &e:entry) {
    if(e.ota_seq==0 || e.ota_seq==UINT32_MAX || e.crc!=ota_boot::computeSeqCrc(e.ota_seq) || e.ota_state!=ESP_OTA_IMG_VALID) continue;
    auto p=esp_partition_find_first(ESP_PARTITION_TYPE_APP,
      static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0+(e.ota_seq-1)%2),nullptr);
    if(p && p->address!=running->address) return p;
  }
  return nullptr;
}
void deadline(void*) {
  if (!probation) return;
  const char* failure = health.failureAtDeadline(millis());
  if (!failure) {
    if (accept()) {
      LOG_INF("BOOT", "Trial accepted after 30 seconds without a startup failure");
      return;
    }
    LOG_ERR("BOOT", "Trial could not be committed; attempting to restore the previous image");
  } else {
    LOG_ERR("BOOT", "Trial failed: %s", failure);
  }
  rollback();
}
}
void begin() {
  const auto running=esp_ota_get_running_partition();
  // NVS must already have been initialised by the Arduino core. Do not erase it
  // if opening/writing fails: leave the OEM pending state unconfirmed.
  if (!verify(running,runningHash)) {previous=priorSlot(running);rollback();esp_restart();return;}
  if(nvs_open("firstboot",NVS_READWRITE,&prefs)!=ESP_OK) {previous=priorSlot(running);rollback();esp_restart();return;}
  size_t n=sizeof record;
  if(nvs_get_blob(prefs,"record",&record,&n)!=ESP_OK || n!=sizeof record || record.magic!=MAGIC) {
    record={}; record.magic=MAGIC;
  }
  if(std::memcmp(record.accepted,runningHash,32)==0) {
    if(record.pending) {record.pending=0;if(!save()) {rollback();esp_restart();return;}}
    if (resetGraceRequiresRollback()) {
      previous=at(record.fallback);
      if (previous) {
        LOG_ERR("BOOT", "Non-sleep reset during the 180-second post-install window; reverting to the previous image");
        rollback();
        return;
      }
      LOG_ERR("BOOT", "Reset during the grace window, but no valid previous image is available");
    }
    esp_ota_img_states_t state;
    if(esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY)
      esp_ota_mark_app_valid_cancel_rollback();
    return;
  }
  probation=true;
  health.reset(millis());
  startResetWindow();
  // A different image starts a new recovery window.
  clearResetGrace();
  uint8_t check[32];
  const uint32_t sequence=runningSequence(running);
  uint32_t priorSequence=0;
  nvs_get_u32(prefs,"trialseq",&priorSequence);
  // Missing sequence on a legacy record is treated conservatively as a failed
  // trial. A different image (including upgrading from .4) gets a fresh trial.
  if(record.pending && std::memcmp(record.trial,runningHash,32)==0 &&
     (!sequence || !priorSequence || sequence==priorSequence)) {
    previous=at(record.fallback);
    rollback(); // a reboot before explicit acceptance is a failed trial
    previous=nullptr; // stay on the blocked trial screen if recovery itself failed
  }
  previous=priorSlot(running);
  if(previous && !verify(previous,check)) previous=nullptr;
  if(!previous) {
    // The OEM may have just rolled this rejected trial back to the accepted app.
    if(std::memcmp(record.accepted,runningHash,32)==0) {
      record.pending=0;
      if(save()) {probation=false;return;}
    }
  }
  record.fallback=previous?previous->address:0;
  std::memcpy(record.trial,runningHash,32);record.pending=1;
  if(!sequence || nvs_set_u32(prefs,"trialseq",sequence)!=ESP_OK || !save()) {
    rollback();esp_restart();return;
  }
  esp_timer_create_args_t args{};args.callback=deadline;args.name="trial_deadline";
  if(esp_timer_create(&args,&timer)!=ESP_OK ||
     esp_timer_start_once(timer,static_cast<uint64_t>(BootTrialHealth::kHealthyWindowMs)*1000ULL)!=ESP_OK) {
    rollback();esp_restart();
  }
}
void observeHealthyLoop(unsigned long now, bool ready) {
  if(probation.load(std::memory_order_acquire)) health.observe(static_cast<uint32_t>(now),ready);
}
bool active(){return probation.load(std::memory_order_acquire);}
bool hasFallback(){return previous!=nullptr;}
void rollback() {
  uint8_t digest[32];
  const auto running=esp_ota_get_running_partition();
  if(!previous || previous->address==running->address || !verify(previous,digest)) return;
  // Persist recovery intent before switching. This also handles rollback after
  // the trial was accepted: if power/reset interrupts the switch, either the
  // old app boots as accepted or the trial image retries the same rollback.
  record.fallback=previous->address;
  std::memcpy(record.accepted,digest,32);
  std::memcpy(record.trial,runningHash,32);
  record.pending=1;
  if (prefs) nvs_erase_key(prefs,"reset_grace");
  if (!save()) return;
  // The previous app itself is never rewritten.
  // A known-good fallback is not a new installation. Marking it NEW makes
  // the OEM bootloader demand another acceptance from older firmware and
  // can abort that recovery image on the next reset.
  if(ota_boot::switchTo(previous, true)) {
    // The old partition has already been selected durably. Only now may the
    // trial be closed, allowing another explicit installation of the same image.
    record.pending=0;
    save(); // failure is conservative: a future reinstall may still be rejected
    esp_restart();
  }
}
bool accept() {
  if(!probation || !previous) return false;
  uint8_t digest[32];if(!verify(previous,digest)) return false;
  esp_ota_img_states_t state;
  if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)!=ESP_OK) return false;
  if(state!=ESP_OTA_IMG_VALID && esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK) return false;
  if (!resetWindowStampValid()) startResetWindow();
  if (nvs_set_u8(prefs,"reset_grace",1)!=ESP_OK || nvs_commit(prefs)!=ESP_OK) return false;
  Record old=record;
  std::memcpy(record.accepted,runningHash,32);record.pending=0;
  if(!save()) {record=old;clearResetGrace();return false;}
  if(timer && esp_timer_is_active(timer)) esp_timer_stop(timer);
  probation=false;
  return true;
}
bool prepareUpdate() {
  if(probation && previous) return false;
  uint8_t digest[32];
  auto run=esp_ota_get_running_partition();
  auto dest=esp_ota_get_next_update_partition(nullptr);
  if(!dest || !run || dest->address==run->address || !verify(run,digest)) return false;
  return true;
}
#else
void begin(){}
void observeHealthyLoop(unsigned long, bool){}
bool active(){return false;}
bool hasFallback(){return false;}
bool accept(){return false;}
void rollback(){}
bool prepareUpdate(){return true;}
#endif
}
