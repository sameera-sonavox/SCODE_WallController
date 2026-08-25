#ifndef FLASH_MEMORY_VALIDATE_TEST_H
#define FLASH_MEMORY_VALIDATE_TEST_H

#include <stdbool.h>

/**
 * @brief Run the external-flash validation suite.
 *
 * Enabled test cases erase and rewrite their configured test range. The
 * suite-level and individual test-case controls are defined in the associated
 * project-definition headers.
 *
 * @return true when every configured validation pass succeeds; otherwise false.
 */
extern bool bRun_FlashMemory_ValidationTests( void );

extern bool bTest_FlashMemory_Partition( void );

#endif // FLASH_MEMORY_VALIDATE_TEST_H
