#include "request.h"

int request_queue_init(request_queue_t *queue, uint32_t capacity)
{
    if (capacity == 0) {
        return -1;
    }

    queue->entries = calloc(capacity, sizeof(request_t));
    if (!queue->entries) {
        return -1;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
    queue->capacity = capacity;
    return 0;
}

void request_queue_destroy(request_queue_t *queue)
{
    free(queue->entries);
    queue->entries = NULL;
    queue->head = queue->tail = queue->size = queue->capacity = 0;
}

bool request_queue_is_full(const request_queue_t *queue)
{
    return queue->size == queue->capacity;
}

bool request_queue_is_empty(const request_queue_t *queue)
{
    return queue->size == 0;
}

bool request_queue_enqueue(request_queue_t *queue, const request_t *request)
{
    if (request_queue_is_full(queue)) {
        return false;
    }

    queue->entries[queue->tail] = *request;
    queue->tail = (queue->tail + 1U) % queue->capacity;
    queue->size++;
    return true;
}

bool request_queue_dequeue(request_queue_t *queue, request_t *request)
{
    if (request_queue_is_empty(queue)) {
        return false;
    }

    *request = queue->entries[queue->head];
    queue->head = (queue->head + 1U) % queue->capacity;
    queue->size--;
    return true;
}
