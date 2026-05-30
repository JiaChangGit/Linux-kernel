#ifndef GC_H
#define GC_H

#include "ftl.h"

/* free block 低於門檻時回傳 true；只判斷，不改變狀態。 */
bool gc_needed(const ftl_context_t *ftl);
/* foreground=true 表示目前 request 已因空間不足被迫等待 GC。 */
bool gc_run(ftl_context_t *ftl, bool foreground);
#endif
