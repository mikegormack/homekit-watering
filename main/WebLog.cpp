#include "WebLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/reent.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace weblog
{
namespace
{
char        s_buf[BUF_SIZE];
uint32_t    s_total;           // bytes ever written; s_buf holds the last BUF_SIZE of them
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
}  // namespace

void init()
{
    s_console = stdout;
    esp_log_set_vprintf(log_vprintf);

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
}  // namespace weblog
