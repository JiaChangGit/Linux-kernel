#ifndef GC_H
#define GC_H

#include "ftl.h"

bool gc_needed(const ftl_context_t *ftl);
bool gc_run(ftl_context_t *ftl, bool foreground);
#endif
