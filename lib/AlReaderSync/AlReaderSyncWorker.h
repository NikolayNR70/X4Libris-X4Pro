#pragma once

#include "AlReaderSyncClient.h"

#include <cstdint>
#include <string>

namespace AlReaderSync {

struct Job {
  enum class Operation : uint8_t { FETCH, UPDATE };

  Operation operation = Operation::FETCH;
  std::string webDavUrl;
  std::string username;
  std::string password;
  std::string bookKey;
  PositionRecord record;
  Result result;
};

namespace Worker {
// The service call, Wi-Fi association and TLS clock preparation all run off the
// UI task. A job owns copied inputs and publishes one result for its sequence.
bool isBusy();
bool post(Job&& job, uint64_t* sequenceOut = nullptr);
bool isDone(uint64_t sequence);
Job consume(uint64_t sequence);
bool drain(unsigned long timeoutMs);
}  // namespace Worker

}  // namespace AlReaderSync
