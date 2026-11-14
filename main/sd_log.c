#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sd_log.h"
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_private/panic_internal.h"
#include "esp32p4/rom/cache.h"
#include "riscv/rvruntime-frames.h"
#include "bsp/m5stack_tab5.h"

static const char *TAG = "SDLOG";

#define RING_SIZE   (128u * 1024u)
#define RING_MAGIC  0x544c4f47u            /* "TLOG" */
#define CRASH_MAGIC 0x43525348u            /* "CRSH" */
#define LOG_DIR     BSP_SD_MOUNT_POINT "/TAB5LOG"
#define FILE_LIMIT  (8u * 1024u * 1024u)   /* start a new file after 8 MB */
#define KEEP_FILES  100u                   /* oldest files are deleted beyond this */
#define FLUSH_MS    5000
#define RETRY_MS    60000                  /* card missing: try again once a minute */
#define CHUNK       4096u

/* Kept across a panic reboot, not across power loss. The PSRAM test at boot
 * skips this section. */
typedef struct { uint32_t magic, size, head, flushed; } ring_t;
EXT_RAM_NOINIT_ATTR static ring_t ring;
EXT_RAM_NOINIT_ATTR static char ring_data[RING_SIZE];

/* Written by the panic handler; internal RAM so it does not depend on PSRAM. */
typedef struct {
    uint32_t magic;
    int core, exception;
    char reason[48], description[48], details[160], task[16];
    uint32_t addr, mepc, ra, sp, mcause, mtval, stack[32];
    int64_t uptime_us;
} crash_t;
static __NOINIT_ATTR crash_t crash;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static FILE *console, *tee, *out;
static TaskHandle_t writer;
static char *recovered, *chunk;
static size_t recovered_len, file_size;
static unsigned file_index;
static bool mounted, warned_missing;
static esp_reset_reason_t reset_reason;

static const char *reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external_pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
    default: return "unknown";
    }
}

static void ring_append(const char *data, size_t n)
{
    if (n > RING_SIZE) { data += n - RING_SIZE; n = RING_SIZE; }
    portENTER_CRITICAL_SAFE(&mux);
    const uint32_t at = ring.head % RING_SIZE;
    const size_t first = RING_SIZE - at < n ? RING_SIZE - at : n;
    memcpy(ring_data + at, data, first);
    memcpy(ring_data, data + first, n - first);
    ring.head += n;
    portEXIT_CRITICAL_SAFE(&mux);
}

/* Copies up to cap unsaved bytes without consuming them; counts bytes that
 * were overwritten before they could be saved. */
static size_t ring_peek(char *dst, size_t cap, uint32_t *lost)
{
    portENTER_CRITICAL(&mux);
    uint32_t pending = ring.head - ring.flushed;
    if (pending > RING_SIZE) {
        *lost += pending - RING_SIZE;
        ring.flushed = ring.head - RING_SIZE;
        pending = RING_SIZE;
    }
    const size_t n = pending < cap ? pending : cap;
    const uint32_t at = ring.flushed % RING_SIZE;
    const size_t first = RING_SIZE - at < n ? RING_SIZE - at : n;
    memcpy(dst, ring_data + at, first);
    memcpy(dst + first, ring_data, n - first);
    portEXIT_CRITICAL(&mux);
    return n;
}

static void ring_consume(size_t n)
{
    portENTER_CRITICAL(&mux);
    ring.flushed += n;
    portEXIT_CRITICAL(&mux);
}

/* Log output goes to the console as before and into the ring. */
static ssize_t tee_write(void *cookie, const char *buf, size_t n)
{
    (void)cookie;
    ring_append(buf, n);
    fwrite(buf, 1, n, console);
    return (ssize_t)n;
}

static int log_vprintf(const char *fmt, va_list args)
{
    return vfprintf(tee, fmt, args);
}

static void copy_text(char *dst, size_t cap, const char *src)
{
    if (!src) return;
    size_t i = 0;
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static bool readable_word(const uint32_t *p)
{
    return ((uintptr_t)p & 3) == 0 && (esp_ptr_in_dram(p) || esp_ptr_external_ram(p));
}

/* Called by ESP-IDF's panic handler before it prints and reboots. */
void __real_esp_panic_handler(panic_info_t *info);
void __wrap_esp_panic_handler(panic_info_t *info)
{
    memset(&crash, 0, sizeof(crash));
    crash.core = info->core;
    crash.exception = info->exception;
    copy_text(crash.reason, sizeof(crash.reason), info->reason);
    copy_text(crash.description, sizeof(crash.description), info->description);
    if (g_panic_abort) copy_text(crash.details, sizeof(crash.details), g_panic_abort_details);
    crash.addr = (uint32_t)(uintptr_t)info->addr;
    const RvExcFrame *frame = info->frame;
    if (frame) {
        crash.mepc = frame->mepc; crash.ra = frame->ra; crash.sp = frame->sp;
        crash.mcause = frame->mcause; crash.mtval = frame->mtval;
        const uint32_t *stack = (const uint32_t *)(uintptr_t)frame->sp;
        for (unsigned i = 0; i < 32 && readable_word(stack + i); ++i) crash.stack[i] = stack[i];
    }
    TaskHandle_t task = xTaskGetCurrentTaskHandleForCore(info->core);
    if (task) copy_text(crash.task, sizeof(crash.task), pcTaskGetName(task));
    crash.uptime_us = esp_timer_get_time();
    crash.magic = CRASH_MAGIC;
    /* The reboot path writes back L1 only; push the ring through L2 into PSRAM. */
    Cache_WriteBack_All(CACHE_MAP_L1_DCACHE);
    Cache_WriteBack_All(CACHE_MAP_L2_CACHE);
    __real_esp_panic_handler(info);
}

static void log_previous_crash(void)
{
    if (crash.magic != CRASH_MAGIC) return;
    crash.magic = 0;
    crash.reason[sizeof(crash.reason) - 1] = 0;
    crash.description[sizeof(crash.description) - 1] = 0;
    crash.details[sizeof(crash.details) - 1] = 0;
    crash.task[sizeof(crash.task) - 1] = 0;
    ESP_LOGE(TAG, "CRASH previous boot: core=%d task=%s reason=%s description=%s details=%s uptime_ms=%lld",
             crash.core, crash.task, crash.reason, crash.description, crash.details,
             (long long)(crash.uptime_us / 1000));
    ESP_LOGE(TAG, "CRASH registers: addr=%08lx mepc=%08lx ra=%08lx sp=%08lx mcause=%08lx mtval=%08lx",
             (unsigned long)crash.addr, (unsigned long)crash.mepc, (unsigned long)crash.ra,
             (unsigned long)crash.sp, (unsigned long)crash.mcause, (unsigned long)crash.mtval);
    for (unsigned i = 0; i < 32; i += 8) {
        const uint32_t *s = crash.stack + i;
        ESP_LOGE(TAG, "CRASH stack+%02u: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", i * 4,
                 (unsigned long)s[0], (unsigned long)s[1], (unsigned long)s[2], (unsigned long)s[3],
                 (unsigned long)s[4], (unsigned long)s[5], (unsigned long)s[6], (unsigned long)s[7]);
    }
}

static bool log_number(const char *name, unsigned *number)
{
    return sscanf(name, "LOG%5u.TXT", number) == 1;
}

/* Deletes the oldest files beyond KEEP_FILES and returns the newest number. */
static unsigned tidy_dir(void)
{
    for (;;) {
        DIR *dir = opendir(LOG_DIR);
        if (!dir) return 0;
        unsigned count = 0, newest = 0, oldest = UINT_MAX, number;
        for (struct dirent *e; (e = readdir(dir));) {
            if (!log_number(e->d_name, &number)) continue;
            ++count;
            if (number > newest) newest = number;
            if (number < oldest) oldest = number;
        }
        closedir(dir);
        if (count < KEEP_FILES) return newest;
        char path[48];
        snprintf(path, sizeof(path), LOG_DIR "/LOG%05u.TXT", oldest);
        if (unlink(path) != 0) return newest;
    }
}

static bool open_file(void)
{
    char path[48];
    snprintf(path, sizeof(path), LOG_DIR "/LOG%05u.TXT", file_index);
    out = fopen(path, "a");
    file_size = 0;
    if (!out) return false;
    char sha[17] = "";
    esp_app_get_elf_sha256(sha, sizeof(sha));
    fprintf(out, "=== Tab5 log %05u | firmware %s %s | reset_reason=%s | uptime_ms=%lld ===\n",
            file_index, esp_app_get_description()->version, sha, reason_name(reset_reason),
            (long long)(esp_timer_get_time() / 1000));
    return true;
}

static void unmount_card(void)
{
    if (out) { fclose(out); out = NULL; }
    bsp_sdcard_unmount();
    mounted = false;
}

static bool mount_card(void)
{
    if (bsp_sdcard_mount() != ESP_OK) {
        bsp_sdcard_unmount();
        if (!warned_missing) ESP_LOGW(TAG, "no microSD card; logs stay in memory only");
        warned_missing = true;
        return false;
    }
    mkdir(LOG_DIR, 0775);
    file_index = tidy_dir() % 99999 + 1;
    if (!open_file()) { ESP_LOGW(TAG, "cannot create a log file"); unmount_card(); return false; }
    mounted = true;
    if (recovered) {
        fprintf(out, "=== previous boot: last %u bytes not yet saved ===\n", (unsigned)recovered_len);
        fwrite(recovered, 1, recovered_len, out);
        fprintf(out, "\n=== end of previous boot ===\n");
        heap_caps_free(recovered); recovered = NULL; recovered_len = 0;
    }
    ESP_LOGI(TAG, "saving logs to " LOG_DIR "/LOG%05u.TXT", file_index);
    return true;
}

static bool flush_ring(void)
{
    uint32_t lost = 0;
    for (;;) {
        const size_t n = ring_peek(chunk, CHUNK, &lost);
        if (lost) { fprintf(out, "\n[SDLOG: %lu bytes overwritten before saving]\n", (unsigned long)lost); lost = 0; }
        if (!n) break;
        if (fwrite(chunk, 1, n, out) != n) return false;
        ring_consume(n);
        file_size += n;
    }
    if (fflush(out) != 0 || fsync(fileno(out)) != 0) return false;
    if (file_size > FILE_LIMIT) {
        fclose(out); out = NULL;
        file_index = file_index % 99999 + 1;
        if (!open_file()) return false;
    }
    return true;
}

static void writer_task(void *arg)
{
    (void)arg;
    for (bool first = true;; first = false) {
        if (!first) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(mounted ? FLUSH_MS : RETRY_MS));
        if (!mounted && !mount_card()) continue;
        if (!flush_ring()) {
            unmount_card();
            ESP_LOGW(TAG, "microSD write failed; will retry");
        }
    }
}

void tab5_log_init(void)
{
    reset_reason = esp_reset_reason();
    /* Only a panic writes the caches back, so only then is the PSRAM ring current.
     * After any other reset the periodic saves are the record. */
    const bool panicked = reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT ||
                          reset_reason == ESP_RST_TASK_WDT;
    if (!panicked) crash.magic = 0;
    if (panicked && ring.magic == RING_MAGIC && ring.size == RING_SIZE) {
        uint32_t pending = ring.head - ring.flushed;
        if (pending > RING_SIZE) pending = RING_SIZE;
        if (pending && (recovered = heap_caps_malloc(pending, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
            const uint32_t at = (ring.head - pending) % RING_SIZE;
            const size_t first = RING_SIZE - at < pending ? RING_SIZE - at : pending;
            memcpy(recovered, ring_data + at, first);
            memcpy(recovered + first, ring_data, pending - first);
            recovered_len = pending;
        }
    }
    ring.head = ring.flushed = 0;
    ring.size = RING_SIZE;
    ring.magic = RING_MAGIC;

    console = stdout;
    static const cookie_io_functions_t io = { .write = tee_write };
    tee = fopencookie(NULL, "w", io);
    chunk = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tee || !chunk) { ESP_LOGE(TAG, "log recorder disabled: out of memory"); return; }
    setvbuf(tee, NULL, _IOLBF, 512);
    esp_log_set_vprintf(log_vprintf);

    ESP_LOGI(TAG, "boot reset_reason=%s recovered_bytes=%u", reason_name(reset_reason), (unsigned)recovered_len);
    log_previous_crash();
    if (xTaskCreate(writer_task, "sd_log", 6144, NULL, 2, &writer) != pdPASS) ESP_LOGE(TAG, "writer task failed");
}

void tab5_log_event(const char *what)
{
    ESP_LOGW(TAG, "EVENT %s uptime_ms=%lld heap_internal=%u psram=%u min_heap=%u", what,
             (long long)(esp_timer_get_time() / 1000), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)esp_get_minimum_free_heap_size());
    if (writer) xTaskNotifyGive(writer);
}
