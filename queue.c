#include <libavutil/frame.h>
#include <SDL2/SDL.h>
#include "queue.h"

bool queue_init(Queue *queue) {
    queue->count = 0;
    queue->fill_ptr = 0;
    queue->use_ptr = 0;
    queue->empty = SDL_CreateCond();
    queue->fill = SDL_CreateCond();
    queue->mutex = SDL_CreateMutex();
    return (
            queue->empty &&
            queue->fill &&
            queue->mutex
           );
}

void queue_fini(Queue *queue) {
    SDL_DestroyCond(queue->empty);
    SDL_DestroyCond(queue->fill);
    SDL_DestroyMutex(queue->mutex);
    queue_flush(queue);
}

void queue_enqueue(Queue *queue, entry_t entry) {
    queue->buffer[queue->fill_ptr] = entry;
    queue->fill_ptr = (queue->fill_ptr + 1) % QUEUE_MAX;
    queue->count++;
}

entry_t queue_dequeue(Queue *queue) {
    entry_t tmp = queue->buffer[queue->use_ptr];
    queue->use_ptr = (queue->use_ptr + 1) % QUEUE_MAX;
    queue->count--;
    return tmp;
}

void queue_flush(Queue *queue) {
    for (int i = queue->use_ptr; i != queue->fill_ptr; i = (i + 1) % QUEUE_MAX) {
        av_frame_free(&queue->buffer[i].frame);
    }
    queue->use_ptr = queue->fill_ptr;
    queue->count = 0;
}
