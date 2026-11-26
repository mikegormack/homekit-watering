#pragma once

#include <stddef.h>
#include <stdint.h>

// Keeps the most recent console output in RAM for the /logs web page: everything logged with
// ESP_LOGx, and printf output from tasks created after weblog_init() (e.g. the HomeKit core).
// The serial console keeps working as before. ANSI colour codes are dropped. The buffer survives
// a crash or software restart (not a power cut), so the lines before a crash can be read after it.
namespace weblog
{
constexpr size_t BUF_SIZE = 8192;

void init();  // call early in app_main, before other tasks are started

// Copies text written after position `since` (a running byte count) into buf, NUL-terminated.
// Returns the position to pass next time. Older text that has been overwritten is skipped.
uint32_t read(uint32_t since, char *buf, size_t len, size_t *out_len);

void clear();

// The previous boot's crash (reason, task, backtrace addresses), "" if it didn't crash. Recorded
// by a wrapper around ESP-IDF's panic handler and kept in RAM that survives the restart.
const char *last_crash();
}  // namespace weblog
