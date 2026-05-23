#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "ftl.h"
#include "nvme.h"
#include "request.h"

bool scheduler_run(ftl_context_t *ftl,
                   request_queue_t *queue,
                   nvme_controller_t *controller);

#endif
