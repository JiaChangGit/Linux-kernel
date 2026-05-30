#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "ftl.h"
#include "nvme.h"
#include "request.h"

/* 從內部 request queue 取出工作，送進 FTL，最後寫入 NVMe CQ。 */
bool scheduler_run(ftl_context_t *ftl,
                   request_queue_t *queue,
                   nvme_controller_t *controller);

#endif
