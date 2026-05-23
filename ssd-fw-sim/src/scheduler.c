#include "scheduler.h"
#include "gc.h"

bool scheduler_run(ftl_context_t *ftl,
                   request_queue_t *queue,
                   nvme_controller_t *controller)
{
    request_t request;

    while (request_queue_dequeue(queue, &request)) {
        uint64_t service_start;

        if (ftl->current_time_us < request.submit_timestamp_us) {
            ftl->current_time_us = request.submit_timestamp_us;
        }

        request.dispatch_timestamp_us = ftl->current_time_us;
        request.queue_latency_us = request.dispatch_timestamp_us - request.submit_timestamp_us;
        service_start = request.dispatch_timestamp_us;

        if (gc_needed(ftl)) {
            (void)gc_run(ftl, false);
        }

        if (!ftl_handle_request(ftl, &request)) {
            (void)nvme_post_completion(controller,
                                       &request,
                                       NVME_STATUS_INTERNAL_ERROR,
                                       ftl->current_time_us);
            LOG_ERROR("Request handling failed, lba=%llu length=%u",
                      (unsigned long long)request.lba,
                      request.length);
            return false;
        }

        request.complete_timestamp_us = ftl->current_time_us;
        request.service_latency_us = request.complete_timestamp_us - service_start;
        request.total_latency_us = request.complete_timestamp_us - request.submit_timestamp_us;

        if (!nvme_post_completion(controller,
                                  &request,
                                  NVME_STATUS_SUCCESS,
                                  request.complete_timestamp_us)) {
            LOG_ERROR("NVMe completion queue is full, command_id=%u",
                      request.command_id);
            return false;
        }

        stats_update_request(&ftl->stats,
                             request.queue_latency_us,
                             request.service_latency_us,
                             request.total_latency_us);

        ftl->stats.host_request_count++;
    }

    return true;
}
