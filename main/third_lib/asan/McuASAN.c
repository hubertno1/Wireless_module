/*
 * Copyright (c) 2021, Erich Styger
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "McuASANconfig.h"
#if McuASAN_CONFIG_IS_ENABLED
#include "McuASAN.h"
#include "McuLog.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_debug_helpers.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_MODULE_MCUASAN 1

#if 1
/* hooks if using -fsanitize=address */
/* -fasan-shadow-offset=number */
/* -fsanitize=kernel-address */
static void __asan_ReportGenericError(void) __attribute__((noreturn));
static void __asan_ReportGenericError(void) {
#if 1
  McuLog_fatal("ASAN generic failure");
  abort();
#else
  __asm volatile("ebreak"); /* stop application */
  for(;;){}
#endif
}

/* below are the required callbacks needed by ASAN */
// 更新后的 ASAN 回调函数
void __asan_report_store1(void *address) __attribute__((noreturn));
void __asan_report_store1(void *address) { __asan_ReportGenericError(); }

void __asan_report_store2(void *address) __attribute__((noreturn));
void __asan_report_store2(void *address) { __asan_ReportGenericError(); }

void __asan_report_store4(void *address) __attribute__((noreturn));
void __asan_report_store4(void *address) { __asan_ReportGenericError(); }

void __asan_report_store_n(void *address, int size) __attribute__((noreturn));
void __asan_report_store_n(void *address, int size) { __asan_ReportGenericError(); }

void __asan_report_load1(void *address) __attribute__((noreturn));
void __asan_report_load1(void *address) { __asan_ReportGenericError(); }

void __asan_report_load2(void *address) __attribute__((noreturn));
void __asan_report_load2(void *address) { __asan_ReportGenericError(); }

void __asan_report_load4(void *address) __attribute__((noreturn));
void __asan_report_load4(void *address) { __asan_ReportGenericError(); }

void __asan_report_load_n(void *address, int size) __attribute__((noreturn));
void __asan_report_load_n(void *address, int size) { __asan_ReportGenericError(); }
#endif

#if 1
static void NYI(void) {
  __asm volatile("ebreak"); /* stop application */             // modified to ebreak
  //for(;;){}
}
void __asan_stack_malloc_1(size_t size, void *addr) { NYI(); }
void __asan_stack_malloc_2(size_t size, void *addr) { NYI(); }
void __asan_stack_malloc_3(size_t size, void *addr) { NYI(); }
void __asan_stack_malloc_4(size_t size, void *addr) { NYI(); }
void __asan_handle_no_return(void) { NYI(); }
void __asan_option_detect_stack_use_after_return(void) { NYI(); }

// 更新 __asan_register_globals 和 __asan_unregister_globals
void __asan_register_globals(void *globals, int n) { NYI(); }
void __asan_unregister_globals(void *globals, int n) { NYI(); }
void __asan_version_mismatch_check_v8(void) { NYI(); }
#endif

/* see https://github.com/gcc-mirror/gcc/blob/master/libsanitizer/asan/asan_interface_internal.h */
// static uint8_t shadow[McuASAN_CONFIG_APP_MEM_SIZE/8]; /* one shadow byte for 8 application memory bytes. A 1 means that the memory address is poisoned */
// extern uint8_t _shadow_start[];
// extern uint8_t _shadow_end[];
// static uint8_t *shadow = _shadow_start;
static uint8_t shadow[McuASAN_CONFIG_APP_MEM_SIZE/8] __attribute__((section(".dram0.shadow")));


#if McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
static void *freeQuarantineList[McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE];
/*!< list of free'd blocks in quarantine */
static int freeQuarantineListIdx; /* index in list (ring buffer), points to free element in list */
#endif

typedef enum {
  kIsWrite, /* write access */
  kIsRead,  /* read access */
} rw_mode_e;

static uint8_t *MemToShadow(void *address) {
  address -= McuASAN_CONFIG_APP_MEM_START;
  return shadow+(((uint32_t)address)>>3); /* divided by 8: every byte has a shadow bit */



  // uintptr_t addr = (uintptr_t)address;
  // if (addr < McuASAN_CONFIG_APP_MEM_START || addr >= (McuASAN_CONFIG_APP_MEM_START + McuASAN_CONFIG_APP_MEM_SIZE)) {
  //   return NULL;
  // }

  // uintptr_t offset = addr - McuASAN_CONFIG_APP_MEM_START;
  // return (uint8_t*) (shadow + (offset >> 3));

}

/**
 * @brief 
 * 
 * @param addr 
 */
static void PoisonShadowByte1Addr(void *addr) {
  if (addr>=(void*)McuASAN_CONFIG_APP_MEM_START && addr<(void*)(McuASAN_CONFIG_APP_MEM_START+McuASAN_CONFIG_APP_MEM_SIZE)) {
    *MemToShadow(addr) |= 1<<((uint32_t)addr&7); /* mark memory in shadow as poisoned with shadow bit */
  }
}

/**
 * @brief 
 * 
 * @param addr 
 */
static void ClearShadowByte1Addr(void *addr) {
  if (addr>=(void*)McuASAN_CONFIG_APP_MEM_START && addr<(void*)(McuASAN_CONFIG_APP_MEM_START+McuASAN_CONFIG_APP_MEM_SIZE)) {
    // uint8_t* mem_to_shadow = MemToShadow(addr);
    //ESP_LOGW("DEBUG_MODULE_MCUASAN", "mem_to_shadow = %p, shadow value before clear = 0x%x", mem_to_shadow, *mem_to_shadow);
    *MemToShadow(addr) &= ~(1<<((uint32_t)addr&7)); /* clear shadow bit: it is a valid memory */
    //ESP_LOGW("DEBUG_MODULE_MCUASAN", "shadow value after clear = 0x%x", *mem_to_shadow);
  }
  //ESP_LOGW("DEBUG_MODULE_MCUASAN", "addr = %p", addr);
}

/**
 * @brief 如果进入这个函数，说明这个内存附近有内存被污染，但是不确定是不是这一个内存被污染。所以要查看这个内存精确的shadow bit是不是对应上。
 * 
 * @param shadow_value 
 * @param address 
 * @param kAccessSize 
 * @return true 说明检查到这个内存被污染，应该触发reportError
 * @return false 
 */
static bool SlowPathCheck(int8_t shadow_value, void *address, size_t kAccessSize) {
  /* return true if access to address is poisoned */
  // int8_t last_accessed_byte = (((uint32_t)address) & 7) + kAccessSize - 1;
  // ESP_LOGW("DEBUG_MODULE_MCUASAN", "last_accessed_byte = %d, shadow_value = %d", last_accessed_byte, shadow_value);
  // return (last_accessed_byte >= shadow_value);

  uint8_t last_accessed_byte = (((uint32_t)address) & 7) + kAccessSize - 1;
  //ESP_LOGW("mcu_asan_module_check", "last_accessed_byte = %d, shadow_value = 0x%x", last_accessed_byte, (uint8_t)shadow_value);
  uint8_t unsigned_shadow = (uint8_t)shadow_value;
  // 需要检查实际的bit位，而不是直接比较数值
  return (unsigned_shadow & (1 << last_accessed_byte)) != 0;


}

static void ReportError(void *address, size_t kAccessSize, rw_mode_e mode) {
  McuLog_fatal("ASAN ptr failure: addr 0x%x, %s, size: %d", (unsigned int)address, mode==kIsRead?"read":"write", kAccessSize);
  // Print the backtrace
  esp_backtrace_print(32); // Adjust depth as needed

  // Terminate the program
  // abort();

}

/**
 * @brief 这只是暂时的我对于CheckShadow函数的理解，注释可能不是函数想要表达的原意。
 * 
 * 这一块可能有问题，他不应该报错。理论上是看这一块对应的shadow bit它是不是0
 * 
 * 
 * @param address           指向某块内存的指针，提供给CheckShadow函数，用于检查该块内存是否被污染。
 * @param kAccessSize       这块待检查的内存的实际大小，单位是bytes。提供给CheckShadow函数，和address一起用于检查这块地址上，这个大小的内存是否被污染。 
 * @param mode 
 */
static void CheckShadow(void *address, size_t kAccessSize, rw_mode_e mode) {
  int8_t *shadow_address;
  int8_t shadow_value;

  if (address>=(void*)McuASAN_CONFIG_APP_MEM_START && address<(void*)(McuASAN_CONFIG_APP_MEM_START+McuASAN_CONFIG_APP_MEM_SIZE)) {
    shadow_address = (int8_t*)MemToShadow(address);
    shadow_value = *shadow_address;
  //ESP_LOGW("McuASAN_CheckShadow", "address of the memory to be checked = %p", address);
  //ESP_LOGW("McuASAN_CheckShadow", "int8_t shadow_value = %d or 0x%x, int8_t* shadow_address = %p ", shadow_value, (uint8_t)shadow_value, shadow_address);

    if (shadow_value==-1) {
      ReportError(address, kAccessSize, mode);
    } else if (shadow_value!=0) { /* fast check: poisoned! */
      //ESP_LOGW("DEBUG_MODULE_McuASAN", "shadow value pass to slowpathcheck = %d", shadow_value);
      //ESP_LOGW("DEBUG_MODULE_McuASAN", "address pass to slowpathcheck = %p", address);
     //ESP_LOGW("DEBUG_MODULE_McuASAN", "kAccessSize pass to slowpathcheck = %d", kAccessSize);
      if (SlowPathCheck(shadow_value, address, kAccessSize)) {    /* 这个可能是错误所在，slowpathcheck理应在这个case下返回0，但是他却返回了1 */
        ReportError(address, kAccessSize, mode);
      }
    }
  }
}

// 实现缺失的 ASAN 函数
void __asan_loadN_noabort(void *address, int size) {
  ESP_LOGW("ASAN_DEBUG", "ENTERED _asan_loadN_noabort module");
  CheckShadow(address, size, kIsRead);
  //NYI();
}

void __asan_storeN_noabort(void *address, int size) {
  ESP_LOGW("ASAN_DEBUG", "ENTERED _asan_storeN_noabort module");
  CheckShadow(address, size, kIsWrite);
  //NYI();
}


void __asan_load8_noabort(void *address) {
  CheckShadow(address, 8, kIsRead);
}

void __asan_store8_noabort(void *address) {
  CheckShadow(address, 8, kIsWrite);
}

void __asan_load4_noabort(void *address) {
  CheckShadow(address, 4, kIsRead); /* check if we are reading from poisoned memory */
}

void __asan_store4_noabort(void *address) {
  //ESP_LOGW("ASAN_DEBUG", "ENTERED _asan_store4_noabort module");
  CheckShadow(address, 4, kIsWrite); /* check if we are writing to poisoned memory */
}

void __asan_load2_noabort(void *address) {
  CheckShadow(address, 2, kIsRead); /* check if we are reading from poisoned memory */
}

void __asan_store2_noabort(void *address) {
  CheckShadow(address, 2, kIsWrite); /* check if we are writing to poisoned memory */
}

void __asan_load1_noabort(void *address) {

  //ESP_LOGW("DEBUG_MODULE_MCUASAN", "enterd _asan_load1_noabort memory need to be checked: address = %p", address);
  CheckShadow(address, 1, kIsRead); /* check if we are reading from poisoned memory */
}

void __asan_store1_noabort(void *address) {
    //ESP_LOGW("DEBUG_MODULE_MCUASAN", "enterd _asan_store1_noabort memory need to be checked: address = %p", address);

  CheckShadow(address, 1, kIsWrite); /* check if we are writing to poisoned memory */
}



#if McuASAN_CONFIG_CHECK_MALLOC_FREE
/* undo possible defines for malloc and free */
#ifdef malloc
  #undef malloc
  void *malloc(size_t);
#endif
#ifdef free
  #undef free
  void free(void*);
#endif
/*
 * rrrrrrrr  red zone border (incl. size below)
 * size
 * memory returned
 * rrrrrrrr  red zone boarder
 */

void *__asan_malloc(size_t size) {
  /* malloc allocates the requested amount of memory with redzones around it.
   * The shadow values corresponding to the redzones are poisoned and the shadow values
   * for the memory region are cleared.
   */
  void *p = malloc(size+2*McuASAN_CONFIG_MALLOC_RED_ZONE_BORDER); /* add size_t for the size of the block */
  //ESP_LOGE("Inital addr", "p = %p", p);
  void *q;

  q = p;
  /* poison red zone at the beginning */
  for(int i=0; i<McuASAN_CONFIG_MALLOC_RED_ZONE_BORDER; i++) {
    PoisonShadowByte1Addr(q);
    q++;
  }
  *((size_t*)(q-sizeof(size_t))) = size; /* store memory size, needed for the free() part */
  /* clear valid memory */
  for(int i=0; i<size; i++) {
    //ESP_LOGW("DEBUG_MODULE_MCUASAN", "q = %p", q);

    ClearShadowByte1Addr(q);
    q++;
  }


  /* poison red zone at the end */
  for(int i=0; i<McuASAN_CONFIG_MALLOC_RED_ZONE_BORDER; i++) {
    PoisonShadowByte1Addr(q);
    q++;
  }
  return p+McuASAN_CONFIG_MALLOC_RED_ZONE_BORDER; /* return pointer to valid memory */
}
#endif

#if McuASAN_CONFIG_CHECK_MALLOC_FREE
void __asan_free(void *p) {
  /* Poisons shadow values for the entire region and put the chunk of memory into a quarantine queue
   * (such that this chunk will not be returned again by malloc during some period of time).
   */
  size_t size = *((size_t*)(p-sizeof(size_t))); /* get size */
  void *q = p;

  for(int i=0; i<size; i++) {
    PoisonShadowByte1Addr(q);
    q++;
  }
  q = p-McuASAN_CONFIG_MALLOC_RED_ZONE_BORDER; /* calculate beginning of malloc()ed block */
#if McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
  /* put the memory block into quarantine */
  freeQuarantineList[freeQuarantineListIdx] = q;
  freeQuarantineListIdx++;
  if (freeQuarantineListIdx>=McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE) {
    freeQuarantineListIdx = 0;
  }
  if (freeQuarantineList[freeQuarantineListIdx]!=NULL) {
    free(freeQuarantineList[freeQuarantineListIdx]);
    freeQuarantineList[freeQuarantineListIdx] = NULL;
  }
#else
  free(q); /* free block */
#endif
}
#endif /* McuASAN_CONFIG_CHECK_MALLOC_FREE */


void McuASAN_Init(void) {

  extern uint8_t _bss_start[], _bss_end[];
  extern uint8_t _data_start[], _data_end[];

  size_t shadow_size = sizeof(shadow);

  //ESP_LOGW("DEBUG_MODULE_MCUASAN", "BSS: %p -> %p", (void *)&_bss_start, (void *)&_bss_end);
  //ESP_LOGW("DEBUG_MODULE_MCUASAN", "DATA: %p -> %p", (void *)&_data_start, (void *)&_data_end);

  //ESP_LOGW("DEBUG_MODULE_MCUASAN", "the address of shadow[0] = %p, the address of shadow[shadow_size - 1] = %p", &shadow[0], &shadow[shadow_size - 1]);
  
  for(int i=0; i<shadow_size; i++) { /* initialize full shadow map */
    shadow[i] = 0; /* poison everything  */
  }

  /* depoison BSS and DATA sections */
    for (uint8_t *p = (uint8_t *)_bss_start; p < (uint8_t *)_bss_end; p++) {
        ClearShadowByte1Addr(p);
    }
    for (uint8_t *p = (uint8_t *)_data_start; p < (uint8_t *)_data_end; p++) {
        ClearShadowByte1Addr(p);
    }
  
#if McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
  for(int i=0; i<McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE; i++) {
    freeQuarantineList[i] = NULL;
  }
  freeQuarantineListIdx = 0;
#endif
}

#endif /* McuASAN_CONFIG_IS_ENABLED */
