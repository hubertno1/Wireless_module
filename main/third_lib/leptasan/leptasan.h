/**
 * Copyright (c) 2024 Haizhu
 *
 * All rights reserved. No part of this code or its documentation may be
 * reproduced or used in any form without prior written permission from the author.
 * Unauthorized use, copying, or distribution is strictly prohibited.
 */

#ifndef __LEPTASAN_H__
#define __LEPTASAN_H__

#include "leptasan_config.h"


/**
 * @brief init the shadow memory to monitor the application memory
 * 
 */
void leptasan_init_shadow(void);

#if LEPTASAN_ENABLE_REPLACE_MALLOC_FREE > 0

#define malloc __leptasan_malloc
#define free   __leptasan_free

#endif

void *__leptasan_malloc(size_t size);
void __leptasan_free(void *p);




#endif /* __LEPTASAN_CORE_H__ */
