#ifndef __LEPTASAN_CONFIG_H__
#define __LEPTASAN_CONFIG_H__

#include "leptasan_config.h"

/**
 * @brief 1: Enable replace malloc and free; 0: Disable replace malloc and free
 */
#define LEPTASAN_ENABLE_REPLACE_MALLOC_FREE     (1)




/**
 * @brief 1: Enable Leptasan internal debugging; 0: Disable internal debugging
 */
#define LEPTASAN_CONFIG_DEBUG (0)


#define LEPTASAN_CONFIG_APP_MEM_START_ADDR  (0x3FC80000)

/**
 * @brief Application memory size
 */
#define LEPTASAN_CONFIG_APP_SIZE  (0x42310)


#define LEPTASAN_CONFIG_SHADOW_MEM_START (0x3FCC2310)


/**
 * @brief Shadow memory size
 */
#define LEPTASAN_CONFIG_SHADOW_SIZE (LEPTASAN_CONFIG_APP_SIZE / 8)

/**
 * @brief Size of the red zone border in bytes. 
 *        Must be a multiple of 8 bytes to maintain alignment requirements.
 */
#define LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE    (8 * 2)


#define LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE  (3)

#endif /* __LEPTASAN_CONFIG_H__ */


