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

#ifdef __cplusplus
extern "C" {
#endif

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
  for(;;){}
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
static uint8_t shadow[McuASAN_CONFIG_APP_MEM_SIZE/8]; /* one shadow byte for 8 application memory bytes. A 1 means that the memory address is poisoned */

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
}

static void PoisonShadowByte1Addr(void *addr) {
  if (addr>=(void*)McuASAN_CONFIG_APP_MEM_START && addr<(void*)(McuASAN_CONFIG_APP_MEM_START+McuASAN_CONFIG_APP_MEM_SIZE)) {
    *MemToShadow(addr) |= 1<<((uint32_t)addr&7); /* mark memory in shadow as poisoned with shadow bit */
  }
}

static void ClearShadowByte1Addr(void *addr) {
  if (addr>=(void*)McuASAN_CONFIG_APP_MEM_START && addr<(void*)(McuASAN_CONFIG_APP_MEM_START+McuASAN_CONFIG_APP_MEM_SIZE)) {
    *MemToShadow(addr) &= ~(1<<((uint32_t)addr&7)); /* clear shadow bit: it is a valid memory */
  }
}

static bool SlowPathCheck(int8_t shadow_value, void *address, size_t kAccessSize) {
  /* return true if access to address is poisoned */
  int8_t last_accessed_byte = (((uint32_t)address) & 7) + kAccessSize - 1;
  return (last_accessed_byte >= shadow_value);
}

static void ReportError(void *address, size_t kAccessSize, rw_mode_e mode) {
  McuLog_fatal("ASAN ptr failure: addr 0x%x, %s, size: %d", (unsigned int)address, mode==kIsRead?"read":"write", kAccessSize);
//  __asm volatile("bkpt #0"); /* stop application if debugger is attached */
}

static void CheckShadow(void *address, size_t kAccessSize, rw_mode_e mode) {
  int8_t *shadow_address;
  int8_t shadow_value;

  if (address>=(void*)McuASAN_CONFIG_APP_MEM_START && address<(void*)(McuASAN_CONFIG_APP_MEM_START+McuASAN_CONFIG_APP_MEM_SIZE)) {
    shadow_address = (int8_t*)MemToShadow(address);
    shadow_value = *shadow_address;
    if (shadow_value==-1) {
      ReportError(address, kAccessSize, mode);
    } else if (shadow_value!=0) { /* fast check: poisoned! */
      if (SlowPathCheck(shadow_value, address, kAccessSize)) {
        ReportError(address, kAccessSize, mode);
      }
    }
  }
}

// 实现缺失的 ASAN 函数
void __asan_loadN_noabort(void *address, int size) {
  CheckShadow(address, size, kIsRead);
}

void __asan_storeN_noabort(void *address, int size) {
  CheckShadow(address, size, kIsWrite);
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
  CheckShadow(address, 4, kIsWrite); /* check if we are writing to poisoned memory */
}

void __asan_load2_noabort(void *address) {
  CheckShadow(address, 2, kIsRead); /* check if we are reading from poisoned memory */
}

void __asan_store2_noabort(void *address) {
  CheckShadow(address, 2, kIsWrite); /* check if we are writing to poisoned memory */
}

void __asan_load1_noabort(void *address) {
  CheckShadow(address, 1, kIsRead); /* check if we are reading from poisoned memory */
}

void __asan_store1_noabort(void *address) {
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
  for(int i=0; i<sizeof(shadow); i++) { /* initialize full shadow map */
    shadow[i] = -1; /* poison everything  */
  }
  /* because the shadow is part of the memory area: poison the shadow */
  for(int i=0; i<sizeof(shadow); i+=8) {
    PoisonShadowByte1Addr(&shadow[i]);
  }
#if McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE > 0
  for(int i=0; i<McuASAN_CONFIG_FREE_QUARANTINE_LIST_SIZE; i++) {
    freeQuarantineList[i] = NULL;
  }
  freeQuarantineListIdx = 0;
#endif
}

#endif /* McuASAN_CONFIG_IS_ENABLED */