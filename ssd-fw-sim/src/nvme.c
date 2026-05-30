#include "nvme.h"

static void nvme_request_from_submission(const nvme_submission_entry_t *submission,
                                         request_t *request)
{
    /* SQ entry 只保存協定層欄位；轉成 request_t 後才交給韌體處理。 */
    request->type = REQUEST_TYPE_WRITE;
    request->command_id = submission->command_id;
    request->queue_id = 1;
    request->lba = submission->slba;
    request->length = submission->nlb;
    request->submit_timestamp_us = submission->submit_timestamp_us;
    request->dispatch_timestamp_us = 0;
    request->complete_timestamp_us = 0;
    request->queue_latency_us = 0;
    request->service_latency_us = 0;
    request->total_latency_us = 0;
}

int nvme_controller_init(nvme_controller_t *controller, uint32_t queue_depth)
{
    if (queue_depth == 0) {
        return -1;
    }

    memset(controller, 0, sizeof(*controller));

    controller->sq_entries = calloc(queue_depth, sizeof(nvme_submission_entry_t));
    if (!controller->sq_entries) {
        return -1;
    }

    controller->cq_entries = calloc(queue_depth, sizeof(nvme_completion_entry_t));
    if (!controller->cq_entries) {
        free(controller->sq_entries);
        controller->sq_entries = NULL;
        return -1;
    }

    controller->sq_capacity = queue_depth;
    controller->cq_capacity = queue_depth;
    controller->cq_phase = 1;
    return 0;
}

void nvme_controller_destroy(nvme_controller_t *controller)
{
    free(controller->sq_entries);
    free(controller->cq_entries);
    memset(controller, 0, sizeof(*controller));
}

bool nvme_sq_is_full(const nvme_controller_t *controller)
{
    return controller->sq_count == controller->sq_capacity;
}

bool nvme_sq_is_empty(const nvme_controller_t *controller)
{
    return controller->sq_count == 0;
}

bool nvme_cq_is_full(const nvme_controller_t *controller)
{
    return controller->cq_count == controller->cq_capacity;
}

bool nvme_cq_is_empty(const nvme_controller_t *controller)
{
    return controller->cq_count == 0;
}

uint32_t nvme_sq_count(const nvme_controller_t *controller)
{
    return controller->sq_count;
}

uint32_t nvme_cq_count(const nvme_controller_t *controller)
{
    return controller->cq_count;
}

int nvme_submit_write(nvme_controller_t *controller,
                      uint64_t slba,
                      uint32_t nlb,
                      uint64_t submit_timestamp_us)
{
    nvme_submission_entry_t *entry;

    if (nvme_sq_is_full(controller)) {
        return -1;
    }

    /* Host 只把 command 放進 SQ；真正執行要等 nvme_issue_pending()。 */
    entry = &controller->sq_entries[controller->sq_tail];
    entry->command_id = controller->next_command_id++;
    entry->slba = slba;
    entry->nlb = nlb;
    entry->opcode = NVME_OPCODE_WRITE;
    entry->submit_timestamp_us = submit_timestamp_us;

    controller->sq_tail = (controller->sq_tail + 1U) % controller->sq_capacity;
    controller->sq_count++;
    controller->submission_count++;
    return 0;
}

uint32_t nvme_issue_pending(nvme_controller_t *controller,
                            request_queue_t *request_queue)
{
    uint32_t issued = 0;

    /* 只要 SQ 有命令且內部 RQ 還有空間，就持續 fetch。 */
    while (!nvme_sq_is_empty(controller) &&
           !request_queue_is_full(request_queue)) {
        const nvme_submission_entry_t *entry = &controller->sq_entries[controller->sq_head];
        request_t request;

        /* 目前只支援 WRITE；未知 opcode 會被消耗但不送進 RQ。 */
        if (entry->opcode != NVME_OPCODE_WRITE) {
            controller->sq_head = (controller->sq_head + 1U) % controller->sq_capacity;
            controller->sq_count--;
            continue;
        }

        nvme_request_from_submission(entry, &request);
        if (!request_queue_enqueue(request_queue, &request)) {
            break;
        }

        controller->sq_head = (controller->sq_head + 1U) % controller->sq_capacity;
        controller->sq_count--;
        controller->dispatch_count++;
        issued++;
    }

    return issued;
}

bool nvme_post_completion(nvme_controller_t *controller,
                          const request_t *request,
                          uint16_t status_code,
                          uint64_t complete_timestamp_us)
{
    nvme_completion_entry_t *entry;

    if (nvme_cq_is_full(controller)) {
        return false;
    }

    /* Completion 保留 command_id，host 才能對回原本的 SQ command。 */
    entry = &controller->cq_entries[controller->cq_tail];
    entry->command_id = request->command_id;
    entry->sq_head = (uint16_t)controller->sq_head;
    entry->sq_id = request->queue_id;
    entry->status = status_code;
    entry->phase = controller->cq_phase;
    entry->complete_timestamp_us = complete_timestamp_us;

    controller->cq_tail = (controller->cq_tail + 1U) % controller->cq_capacity;
    controller->cq_count++;
    controller->completion_count++;

    /* CQ 環形回繞時翻轉 phase，模擬 NVMe 判斷 entry 新舊的機制。 */
    if (controller->cq_tail == 0) {
        controller->cq_phase ^= 1U;
    }

    return true;
}

uint32_t nvme_reap_completions(nvme_controller_t *controller)
{
    uint32_t reaped = 0;

    while (!nvme_cq_is_empty(controller)) {
        controller->cq_head = (controller->cq_head + 1U) % controller->cq_capacity;
        controller->cq_count--;
        controller->reaped_completion_count++;
        reaped++;
    }

    return reaped;
}

bool nvme_has_pending(const nvme_controller_t *controller,
                      const request_queue_t *request_queue)
{
    return !nvme_sq_is_empty(controller) ||
           !request_queue_is_empty(request_queue) ||
           !nvme_cq_is_empty(controller);
}
