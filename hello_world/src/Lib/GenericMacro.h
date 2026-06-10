#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define ANSI_RED     "\x1b[31m"
#define ANSI_RESET   "\x1b[0m"

#define FHALT(fmt, ...)                         \
                                                printk(ANSI_RED "fHALT : %s, %d, " fmt "\n" ANSI_RESET, \
                                                __FILE__, __LINE__, ##__VA_ARGS__)

#define bIsTimeOut(tStart, tCurrent, tLimit)    ((tCurrent - tStart) >= tLimit)

