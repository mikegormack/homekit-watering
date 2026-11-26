#include "WebLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/reent.h>

#include <esp_attr.h>
#include <esp_cpu_utils.h>
#include <esp_debug_helpers.h>
#include <esp_log.h>
#include <esp_memory_utils.h>
#include <esp_private/panic_internal.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <xtensa_context.h>

namespace weblog
{
namespace
{
// The log ring lives in "no-init" RAM: kept across a crash or software restart (not power-up),
// so the lines leading up to a crash can still be read afterwards.
constexpr uint32_t LOG_MAGIC = 0x574C4F47;  // "WLOG"
struct LogStore
{
    uint32_t magic;
    uint32_t total;  // bytes ever written; buf holds the last BUF_SIZE of them
    char     buf[BUF_SIZE];
};
__NOINIT_ATTR LogStore s_store;
#define s_buf   s_store.buf
#define s_total s_store.total

// Crash record written by the panic handler wrapper below, reported on the next boot.
constexpr uint32_t CRASH_MAGIC = 0x43525348;  // "CRSH"
constexpr int      CRASH_DEPTH = 16;
struct CrashRecord
{
    uint32_t    magic;
    int         core;
    const char *reason;       // strings in this firmware's flash; still valid after the restart
    const char *description;
    const void *addr;
    char        task[16];
    int         depth;
    uint32_t    pc[CRASH_DEPTH];
};
__NOINIT_ATTR CrashRecord s_crash;
char s_last_crash[512];  // text of the previous boot's crash, "" if none

int         s_esc;             // ANSI escape sequence state: 0 none, 1 after ESC, 2 inside "ESC["
FILE       *s_console;         // the original stdout (UART console)
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void append(const char *p, size_t n)
{
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < n; i++)
    {
        char c = p[i];
        if (s_esc == 1)
        {
            s_esc = (c == '[') ? 2 : 0;
            continue;
        }
        if (s_esc == 2)
        {
            if ((c >= '@' && c <= '~'))
                s_esc = 0;
            continue;
        }
        if (c == '\x1b')
        {
            s_esc = 1;
            continue;
        }
        if (c == '\r')
            continue;
        s_buf[s_total % BUF_SIZE] = c;
        s_total++;
    }
    portEXIT_CRITICAL(&s_lock);
}

// ESP_LOGx from every task: copy to the ring, then print to the console as before.
int log_vprintf(const char *fmt, va_list ap)
{
    char    line[256];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(line, sizeof(line), fmt, ap2);
    va_end(ap2);
    if (n > 0)
        append(line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);
    return s_console ? vfprintf(s_console, fmt, ap) : vprintf(fmt, ap);
}

// printf from tasks created after init(): their stdout is this stream.
int stdout_write(void *cookie, const char *data, int len)
{
    append(data, (size_t)len);
    if (s_console)
    {
        fwrite(data, 1, (size_t)len, s_console);
        fflush(s_console);
    }
    return len;
}

const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r)
    {
        case ESP_RST_SW:       return "software restart";
        case ESP_RST_PANIC:    return "crash (panic)";
        case ESP_RST_INT_WDT:  return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_DEEPSLEEP:return "deep sleep";
        case ESP_RST_EXT:      return "reset pin";
        default:               return "restart";
    }
}

// A string pointer saved before the restart, if it points at readable firmware data.
const char *saved_str(const char *p)
{
    return (p && (esp_ptr_in_drom(p) || esp_ptr_in_dram(p))) ? p : nullptr;
}

void report_crash()
{
    if (s_crash.magic != CRASH_MAGIC)
        return;
    s_crash.magic = 0;

    const char *reason = saved_str(s_crash.reason);
    const char *desc   = saved_str(s_crash.description);
    char        task[sizeof(s_crash.task)];
    memcpy(task, s_crash.task, sizeof(task));
    task[sizeof(task) - 1] = '\0';

    int n = snprintf(s_last_crash, sizeof(s_last_crash), "%s%s%s%s in task '%s' (core %d) at %p. Backtrace:",
                     reason ? reason : "panic", desc ? " (" : "", desc ? desc : "", desc ? ")" : "", task,
                     s_crash.core, s_crash.addr);
    int depth = s_crash.depth < 0 ? 0 : s_crash.depth > CRASH_DEPTH ? CRASH_DEPTH : s_crash.depth;
    for (int i = 0; i < depth && n > 0 && n < (int)sizeof(s_last_crash) - 12; i++)
        n += snprintf(s_last_crash + n, sizeof(s_last_crash) - n, " 0x%08lx", (unsigned long)s_crash.pc[i]);

    ESP_LOGE("crash", "LAST CRASH: %s", s_last_crash);
    ESP_LOGE("crash", "Decode with: xtensa-esp32-elf-addr2line -pfiaC -e build/sprinkler.elf <addresses above>");
}

}  // namespace

void init()
{
    s_console = stdout;
    esp_log_set_vprintf(log_vprintf);

    // Keep the previous run's log after a crash or software restart; start fresh after power-up.
    esp_reset_reason_t rr = esp_reset_reason();
    bool keep = s_store.magic == LOG_MAGIC && rr != ESP_RST_POWERON && rr != ESP_RST_BROWNOUT && rr != ESP_RST_UNKNOWN;
    if (!keep)
    {
        s_store.magic = LOG_MAGIC;
        s_total       = 0;
        s_crash.magic = 0;
    }
    else
    {
        char mark[64];
        int  n = snprintf(mark, sizeof(mark), "\n===== restarted: %s =====\n", reset_reason_str(rr));
        append(mark, (size_t)n);
    }
    report_crash();

    // New tasks copy their stdout from the global reent; point it (and this task's) at a stream
    // that writes to both the console and the ring. Line buffered so output appears per line.
    FILE *f = funopen(nullptr, nullptr, stdout_write, nullptr, nullptr);
    if (f)
    {
        setvbuf(f, nullptr, _IOLBF, 128);
        _GLOBAL_REENT->_stdout = f;
        stdout                 = f;
    }
}

uint32_t read(uint32_t since, char *buf, size_t len, size_t *out_len)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t total  = s_total;
    uint32_t oldest = total > BUF_SIZE ? total - BUF_SIZE : 0;
    if (since < oldest || since > total)
        since = oldest;
    size_t n = total - since;
    if (n > len - 1)
        n = len - 1;
    for (size_t i = 0; i < n; i++)
        buf[i] = s_buf[(since + i) % BUF_SIZE];
    portEXIT_CRITICAL(&s_lock);
    buf[n]   = '\0';
    *out_len = n;
    return since + n;
}

void clear()
{
    portENTER_CRITICAL(&s_lock);
    s_total = 0;
    portEXIT_CRITICAL(&s_lock);
}

const char *last_crash()
{
    return s_last_crash;
}
}  // namespace weblog

// ---------------------------------------------------------------------------
// Panic handler wrapper (linked with -Wl,--wrap=esp_panic_handler, see main/CMakeLists.txt):
// saves the crash reason, task and backtrace for the next boot, then runs the normal handler,
// which prints the full report to the serial console and restarts.
// ---------------------------------------------------------------------------

extern "C" void __real_esp_panic_handler(panic_info_t *info);

extern "C" void __wrap_esp_panic_handler(panic_info_t *info)
{
    using namespace weblog;
    s_crash.magic       = 0;
    s_crash.core        = info->core;
    s_crash.reason      = info->reason;
    s_crash.description = info->description;
    s_crash.addr        = info->addr;
    s_crash.depth       = 0;

    // Name of the task that was running on the crashing core
    memset(s_crash.task, 0, sizeof(s_crash.task));
    TaskHandle_t t = xTaskGetCurrentTaskHandleForCore(info->core);
    const char  *name = t ? pcTaskGetName(t) : nullptr;
    if (name && esp_ptr_in_dram(name))
        strncpy(s_crash.task, name, sizeof(s_crash.task) - 1);

    // Backtrace, walked the same way as the panic handler's own printout
    const XtExcFrame *xf = static_cast<const XtExcFrame *>(info->frame);
    if (xf)
    {
        esp_backtrace_frame_t fr = {(uint32_t)xf->pc, (uint32_t)xf->a1, (uint32_t)xf->a0, xf};
        s_crash.pc[s_crash.depth++] = esp_cpu_process_stack_pc(fr.pc);
        while (s_crash.depth < CRASH_DEPTH && fr.next_pc != 0 && esp_stack_ptr_is_sane(fr.sp) &&
               esp_backtrace_get_next_frame(&fr))
            s_crash.pc[s_crash.depth++] = esp_cpu_process_stack_pc(fr.pc);
    }
    s_crash.magic = CRASH_MAGIC;

    __real_esp_panic_handler(info);
}
