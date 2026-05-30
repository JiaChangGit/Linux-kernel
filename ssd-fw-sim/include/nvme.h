#ifndef NVME_H
#define NVME_H

#include "request.h"

/* Host 放進 Submission Queue 的簡化 NVMe write command。 */
typedef struct {
    uint16_t command_id;
    uint64_t slba;
    uint32_t nlb;
    uint8_t opcode;
    uint64_t submit_timestamp_us;
} nvme_submission_entry_t;

/* Controller 放進 Completion Queue 的完成項。 */
typedef struct {
    uint16_t command_id;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t status;
    uint8_t phase;
    uint64_t complete_timestamp_us;
} nvme_completion_entry_t;

/* 單一 NVMe controller 的 SQ/CQ 狀態；本專案只模擬一組 queue。 */
typedef struct {
    nvme_submission_entry_t *sq_entries;
    nvme_completion_entry_t *cq_entries;
    uint32_t sq_head;
    uint32_t sq_tail;
    uint32_t sq_count;
    uint32_t sq_capacity;
    uint32_t cq_head;
    uint32_t cq_tail;
    uint32_t cq_count;
    uint32_t cq_capacity;
    uint16_t next_command_id;
    uint8_t cq_phase;
    uint64_t submission_count;
    uint64_t dispatch_count;
    uint64_t completion_count;
    uint64_t reaped_completion_count;
} nvme_controller_t;

/* 目前只支援 WRITE opcode。 */
enum {
    NVME_OPCODE_WRITE = 0x01,
};

/* 簡化版 NVMe status code，足以描述成功、欄位錯誤與內部錯誤。 */
enum {
    NVME_STATUS_SUCCESS = 0x0000,
    NVME_STATUS_INVALID_FIELD = 0x0002,
    NVME_STATUS_INTERNAL_ERROR = 0x0006,
};

int nvme_controller_init(nvme_controller_t *controller, uint32_t queue_depth);
void nvme_controller_destroy(nvme_controller_t *controller);

bool nvme_sq_is_full(const nvme_controller_t *controller);
bool nvme_sq_is_empty(const nvme_controller_t *controller);
bool nvme_cq_is_full(const nvme_controller_t *controller);
bool nvme_cq_is_empty(const nvme_controller_t *controller);

uint32_t nvme_sq_count(const nvme_controller_t *controller);
uint32_t nvme_cq_count(const nvme_controller_t *controller);

/* Host submit；成功後 command 仍在 SQ，尚未進入韌體 request queue。 */
int nvme_submit_write(nvme_controller_t *controller,
                      uint64_t slba,
                      uint32_t nlb,
                      uint64_t submit_timestamp_us);
/* Controller fetch；把 SQ entry 轉成 request_t 並送進內部 queue。 */
uint32_t nvme_issue_pending(nvme_controller_t *controller,
                            request_queue_t *request_queue);
/* Controller complete；scheduler 處理完 request 後用此 API 回報結果。 */
bool nvme_post_completion(nvme_controller_t *controller,
                          const request_t *request,
                          uint16_t status_code,
                          uint64_t complete_timestamp_us);
uint32_t nvme_reap_completions(nvme_controller_t *controller);
bool nvme_has_pending(const nvme_controller_t *controller,
                      const request_queue_t *request_queue);

#endif
