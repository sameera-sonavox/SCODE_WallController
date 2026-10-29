#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>

/* Inspect these in the debugger if startup stalls before UART is ready. */
const struct init_entry * volatile g_boot_last_entry;
volatile int g_boot_last_level;
volatile int g_boot_last_result;
volatile bool g_boot_init_in_progress;
static bool console_ready;

static int boot_trace_console_ready(void)
{
    console_ready = true;
    printk("\r\n[init] console ready; tracing startup before main\r\n");
    return 0;
}
SYS_INIT(boot_trace_console_ready, PRE_KERNEL_1, 61);

void sys_trace_sys_init_enter_user(const struct init_entry *entry, int level)
{
    g_boot_last_entry = entry;
    g_boot_last_level = level;
    g_boot_init_in_progress = true;
    if (console_ready) {
        printk("[init] ENTER level=%d device=%s fn=%p\r\n", level,
               entry->dev ? entry->dev->name : "(system)",
               (void *)entry->init_fn);
    }
}

void sys_trace_sys_init_exit_user(const struct init_entry *entry, int level, int result)
{
    g_boot_last_result = result;
    g_boot_init_in_progress = false;
    if (console_ready) {
        printk("[init] EXIT  level=%d device=%s result=%d\r\n", level,
               entry->dev ? entry->dev->name : "(system)", result);
    }
}
