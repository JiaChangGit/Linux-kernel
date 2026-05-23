#ifndef REQUEST_H
#define REQUEST_H

#include "common.h"

typedef enum {
    REQUEST_TYPE_WRITE = 0,
} request_type_t;

typedef struct {
    request_type_t type;
    uint16_t command_id;
    uint16_t queue_id;
    uint64_t lba;
    uint32_t length;
    uint64_t submit_timestamp_us;
    uint64_t dispatch_timestamp_us;
    uint64_t complete_timestamp_us;
    uint64_t queue_latency_us;
    uint64_t service_latency_us;
    uint64_t total_latency_us;
} request_t;

typedef struct {
    request_t *entries;
    uint32_t head;
    uint32_t tail;
    uint32_t size;
    uint32_t capacity;
} request_queue_t;

int request_queue_init(request_queue_t *queue, uint32_t capacity);
void request_queue_destroy(request_queue_t *queue);
bool request_queue_enqueue(request_queue_t *queue, const request_t *request);
bool request_queue_dequeue(request_queue_t *queue, request_t *request);
bool request_queue_is_full(const request_queue_t *queue);
bool request_queue_is_empty(const request_queue_t *queue);

#endif
