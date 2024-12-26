/*
 * LeptASAN - Lightweight Embedded Platform Thread AddressSanitizer
 * 
 * This module implements the runtime library for address sanitizer on ESP32-C3.
 * It provides memory error detection through shadow memory and red zones.
 */


#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "leptasan_config.h"
#include "leptasan.h"
#include "esp_log.h"
#include "leptasan_log.h"


/* Module debug tag */
static const char *TAG = "LEPTASAN_INTERNAL_DEBUG";

/* Align value x to boundary a */
#define ALIGN(x, a)     (((x) + ((a) - 1)) & ~((a) - 1))

/* Memory access type enumeration */
typedef enum {
  write_access, /* write access */
  read_access,  /* read access */
} rw_mode_e;

/* Shadow memory array, placed in .dram0.shadow section */
static uint8_t leptasan_shadow[LEPTASAN_CONFIG_SHADOW_SIZE] __attribute__((section(".dram0.shadow")));

/* Quarantine list for delayed memory freeing to better detect UAF errors */
#if LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
    static void *leptasan_free_quarantine_list[LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE];
    static int leptasan_free_quarantine_list_idx;
#endif


/* NOT YET IMPLEMENTED */
static void NYI(void) {
  __asm volatile("ebreak"); /* stop application */
}


/* Report memory access error with address and size information */
static void leptasan_report_error(void *address, size_t size, rw_mode_e mode)
{
    LEPTASAN_LOG("memory access error: addr 0x%x, %s, size: %d", (unsigned int)address, mode == write_access ? "write" : "read", size);

    /* TODO: Print the backtrace */
}


/* Calculate shadow value for partial chunks of memory */
static inline uint8_t leptasan_calc_shadow_value(size_t remainder)
{
    switch(remainder)
    {
        case 1: { return 0xFE;}
        case 2: { return 0xFC;}
        case 3: { return 0xF8;}
        case 4: { return 0xF0;}
        case 5: { return 0xE0;}
        case 6: { return 0xC0;}
        case 7: { return 0x80;}
        default: 
        { 
            ESP_LOGE(TAG, "leptasan_calc_shadow_value : IMPOSSIBLE! remainder is not in the range of 1 to 7");
            return 0xFF;
        }
    }
}


/* Check if memory access is valid */
static void leptasan_check_shadow(void *address_to_check, size_t size, rw_mode_e mode)
{
    /* Validate address range */
    if ((uintptr_t)address_to_check < LEPTASAN_CONFIG_APP_MEM_START_ADDR || (uintptr_t)address_to_check >= (LEPTASAN_CONFIG_APP_MEM_START_ADDR + LEPTASAN_CONFIG_APP_SIZE))
    {
        ESP_LOGE(TAG, "address_to_check is not in app mem range");
        return;
    }


    uint8_t* shadow_byte_addr = (uint8_t*) ((((uintptr_t)address_to_check + size - 1 - LEPTASAN_CONFIG_APP_MEM_START_ADDR) >> 3) + LEPTASAN_CONFIG_SHADOW_MEM_START);
    uint8_t shadow_value = *shadow_byte_addr;
    size_t shadow_bit = 1 + (((uintptr_t)address_to_check + size - 1 - LEPTASAN_CONFIG_APP_MEM_START_ADDR) & 0x7); 

    #if LEPTASAN_CONFIG_DEBUG > 0
        ESP_LOGW(TAG, "shadow_byte_addr: %p, shadow_value: 0x%x, shadow_bit: %d", shadow_byte_addr, shadow_value, shadow_bit);
    #endif

    if (shadow_value == 0x00) {
        return;
    }
    if (shadow_value == 0xFF) {
        leptasan_report_error(address_to_check, size, mode);
        return;
    }

    if (shadow_value == leptasan_calc_shadow_value(shadow_bit)) {
        return;
    }
    leptasan_report_error(address_to_check, size, mode);

}


void __asan_handle_no_return(void) { NYI(); }
void __asan_loadN_noabort(void *address, int size) { NYI(); }
void __asan_storeN_noabort(void *address, int size) { NYI(); }

void __asan_load8_noabort(void *address) {
    leptasan_check_shadow(address, 8, read_access);
}

void __asan_store8_noabort(void *address) {
    leptasan_check_shadow(address, 8, write_access);
}

void __asan_load4_noabort(void *address) {
    leptasan_check_shadow(address, 4, read_access); 
}

void __asan_store4_noabort(void *address) {
    leptasan_check_shadow(address, 4, write_access); 
}

void __asan_load2_noabort(void *address) {
    leptasan_check_shadow(address, 2, read_access); 
}

void __asan_store2_noabort(void *address) {
    leptasan_check_shadow(address, 2, write_access); 
}

void __asan_load1_noabort(void *address) {
    leptasan_check_shadow(address, 1, read_access); /* check if we are reading from poisoned memory */
}

void __asan_store1_noabort(void *address) {
    leptasan_check_shadow(address, 1, write_access); /* check if we are writing to poisoned memory */
}


void leptasan_init_shadow(void)
{
    /* Clear shadow memory */
    memset(leptasan_shadow, 0, LEPTASAN_CONFIG_SHADOW_SIZE);

    #if LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
        for (int i = 0; i < LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE; i++)
        {
            leptasan_free_quarantine_list[i] = NULL;
        }
        leptasan_free_quarantine_list_idx = 0;
    #endif

#if LEPTASAN_CONFIG_DEBUG > 0

    bool is_zero = true;
    for (size_t i = 0; i < LEPTASAN_CONFIG_SHADOW_SIZE; i++)
    {
        if (leptasan_shadow[i] != 0)
        {
            is_zero = false;
            ESP_LOGE(TAG, "shadow memory initialization failed at idx %d", i);
            break;
        }
    }
    if (is_zero)
    {
        ESP_LOGW(TAG, "shadow memory initialization all 0 successfully");
    }


    ESP_LOGW(TAG, "shadow memory start addr: %p", (void *)&leptasan_shadow[0]);
    ESP_LOGW(TAG, "shadow memory end addr: %p", (void *)&leptasan_shadow[LEPTASAN_CONFIG_SHADOW_SIZE - 1]);
    

    uintptr_t start_addr = (uintptr_t)&leptasan_shadow[0];
    uintptr_t end_addr = (uintptr_t)&leptasan_shadow[LEPTASAN_CONFIG_SHADOW_SIZE - 1];
    size_t actual_size = (size_t)(end_addr - start_addr + 1);

    if (actual_size == LEPTASAN_CONFIG_SHADOW_SIZE)
    {
        ESP_LOGW(TAG, "shadow memory size verified %u bytes", actual_size);
    }
    else
    {
        ESP_LOGE(TAG, "shadow memory size verification failed, expected %u bytes, actual %u bytes", LEPTASAN_CONFIG_SHADOW_SIZE, actual_size);
    }

#endif
}


static int leptasan_poison_shadow_region(void *p_app_mem_base, size_t size)
{
    if (((uintptr_t)p_app_mem_base & (uintptr_t)0x7) != 0) {
        #if LEPTASAN_CONFIG_DEBUG > 0
            ESP_LOGE(TAG, "leptasan_poison_shadow_region : p_app_mem_base is not 8-byte aligned");
        #endif
        return 1;
    }


    if ((uintptr_t)p_app_mem_base < LEPTASAN_CONFIG_APP_MEM_START_ADDR || ((uintptr_t)p_app_mem_base + (uintptr_t)size) > (LEPTASAN_CONFIG_APP_MEM_START_ADDR + LEPTASAN_CONFIG_APP_SIZE))
    {
        #if LEPTASAN_CONFIG_DEBUG > 0
            ESP_LOGE(TAG, "leptasan_poison_shadow_region : p_app_mem_base %p is not in the range of app mem", p_app_mem_base);
        #endif

        return 2;
    }

    if (size == 0)
    {
        #if LEPTASAN_CONFIG_DEBUG > 0
            ESP_LOGW(TAG, "leptasan_poison_shadow_region : size is 0, nothing to do");
        #endif
        return 0;
    }

    /*
        (Bin)                    (Hex)                       int8_t     (Decimal) Meaning (indicating how many bytes are non-poisoned)
        11111111                 0xFF                        -1         0 bytes non-poisoned (fully poisoned)
        11111110                 0xFE                        -2         1 byte non-poisoned
        11111100                 0xFC                        -4         2 bytes non-poisoned
        11111000                 0xF8                        -8         3 bytes non-poisoned
        11110000                 0xF0                        -16        4 bytes non-poisoned
        11100000                 0xE0                        -32        5 bytes non-poisoned
        11000000                 0xC0                        -64        6 bytes non-poisoned
        10000000                 0x80                        -128       7 bytes non-poisoned
        00000000                 0x00                         0         8 bytes non-poisoned (fully non-poisoned)
    */
    
    uint8_t* p_shadow_addr = (uint8_t*) ((uintptr_t)leptasan_shadow + (((uintptr_t)p_app_mem_base - LEPTASAN_CONFIG_APP_MEM_START_ADDR) >> 3));
    
    size_t full_bytes_to_poisoned = size >> 3;
    size_t remaining_bits_to_poisoned = size & 0x7;

    for (size_t i = 0; i < full_bytes_to_poisoned; i++)
    {
        p_shadow_addr[i] = 0xFF;
    }

    if (remaining_bits_to_poisoned > 0)
    {
        ESP_LOGE(TAG, "leptasan_poison_shadow_region : impossible to have");
    }

    #if LEPTASAN_CONFIG_DEBUG > 0
             
    #endif

    return 0;
}


static int leptasan_unpoison_shadow_region(void *p_app_mem_base, size_t size)
{
    if (((uintptr_t)p_app_mem_base & (uintptr_t)0x7) != 0) {
        #if LEPTASAN_CONFIG_DEBUG > 0
            ESP_LOGE(TAG, "leptasan_unpoison_shadow_region : p_app_mem_base is not 8-byte aligned");
        #endif
        return 1;
    }


    if ((uintptr_t)p_app_mem_base < LEPTASAN_CONFIG_APP_MEM_START_ADDR || ((uintptr_t)p_app_mem_base + (uintptr_t)size) > (LEPTASAN_CONFIG_APP_MEM_START_ADDR + LEPTASAN_CONFIG_APP_SIZE))
    {
        #if LEPTASAN_CONFIG_DEBUG > 0
            ESP_LOGE(TAG, "leptasan_unpoison_shadow_region : p_app_mem_base %p is not in the range of app mem", p_app_mem_base);
        #endif

        return 2;
    }


    if (size == 0)
    {
        #if LEPTASAN_CONFIG_DEBUG > 0
            ESP_LOGW(TAG, "leptasan_unpoison_shadow_region : size is 0, nothing to do");
        #endif
        return 0;
    }

    /* unpoinson the shadow memory */
    uint8_t* p_shadow_addr = (uint8_t*) ((uintptr_t)leptasan_shadow + (((uintptr_t)p_app_mem_base - LEPTASAN_CONFIG_APP_MEM_START_ADDR) >> 3));
    
    size_t full_bytes_to_unpoison = size >> 3;
    size_t remaining_bits_to_unpoison = size & 0x7;

    for (size_t i = 0; i < full_bytes_to_unpoison; i++)
    {
        p_shadow_addr[i] = 0x00;
    }

    if (remaining_bits_to_unpoison > 0)
    {
        uint8_t shadow_value = leptasan_calc_shadow_value(remaining_bits_to_unpoison);
        p_shadow_addr[full_bytes_to_unpoison] = shadow_value;
    }

    #if LEPTASAN_CONFIG_DEBUG > 0
        ;      
    #endif

    return 0;

}

#if LEPTASAN_ENABLE_REPLACE_MALLOC_FREE > 0

#ifdef malloc
    #undef malloc
    void *malloc(size_t size);
#endif
#ifdef free
    #undef free
    void free(void *p);
#endif

#endif

#if LEPTASAN_ENABLE_REPLACE_MALLOC_FREE > 0

void *__leptasan_malloc(size_t size)
{
    /* 1. Allocate more bytes with malloc, placing two red zones at both ends of the size; keep the pointer to the valid memory for the user */
    void* p_first_red_zone = malloc(size + 2 * LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE);
    void* p_valid_memory = (void *) ((uintptr_t)p_first_red_zone + LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE);


    #if LEPTASAN_CONFIG_DEBUG > 0
    if (!p_first_red_zone) {
        ESP_LOGE(TAG, "__leptasan_malloc : malloc failed");
    }
    ESP_LOGW(TAG, "__leptasan_malloc : malloced base addr: %p and start of valid memory addr: %p", p_first_red_zone, p_valid_memory);
    #endif

    /* 2. Poison the shadow byte corresponding to the first red zone, and place useful information in the last size_t of the first red zone: the actual size to malloc, for convenient subsequent free */
    leptasan_poison_shadow_region(p_first_red_zone, LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE);
    size_t *p_store_size = (size_t *)((uintptr_t)p_first_red_zone + (LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE) - sizeof(size_t));
    *p_store_size = size;

    /* 3. Unpoison the memory of actual size */
    leptasan_unpoison_shadow_region(p_valid_memory, size);

    /* 4. Poison the second red zone */
    void* p_second_red_zone = (void *) ((uintptr_t)p_valid_memory + ALIGN(size, 8));
    leptasan_poison_shadow_region(p_second_red_zone, LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE);

    /* 5. Return pointer to valid memory */
    return p_valid_memory;

}

#endif

#if LEPTASAN_ENABLE_REPLACE_MALLOC_FREE > 0

void __leptasan_free(void *p)
{
    void *p_valid_memory = p;

    /* 1. Get the size needed to be freed */
    size_t size = *((size_t *)((uintptr_t)p_valid_memory - sizeof(size_t)));

    // Poison the valid memory before freeing
    leptasan_poison_shadow_region(p_valid_memory, ALIGN(size, 8));

    // Prepare the pointer for free
    void *p_first_red_zone = (void *)((uintptr_t)p_valid_memory - LEPTASAN_CONFIG_RED_ZONE_BORDER_SIZE);

    #if LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
        leptasan_free_quarantine_list[leptasan_free_quarantine_list_idx] = p_first_red_zone;
        leptasan_free_quarantine_list_idx++;
        if (leptasan_free_quarantine_list_idx >= LEPTASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE)
        {
            leptasan_free_quarantine_list_idx = 0;
        }
        if (leptasan_free_quarantine_list[leptasan_free_quarantine_list_idx] != NULL)
        {
            free(leptasan_free_quarantine_list[leptasan_free_quarantine_list_idx]);
            leptasan_free_quarantine_list[leptasan_free_quarantine_list_idx] = NULL;
        }
    #else
        free(p_first_red_zone);
    #endif

}

#endif
