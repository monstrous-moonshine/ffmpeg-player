#include <libavutil/frame.h>
#include <SDL2/SDL.h>
#ifdef QUEUE_LOG_COUNT
#include <stdio.h>
#endif
#include "queue.h"

bool queue_init(Queue *queue, const char *name) {
    queue->count = 0;
    queue->fill_ptr = 0;
    queue->use_ptr = 0;
    queue->empty = SDL_CreateCond();
    queue->fill = SDL_CreateCond();
    queue->mutex = SDL_CreateMutex();
#ifdef QUEUE_LOG_COUNT
    queue->fp = fopen(name, "w");
#else
    (void)name;
#endif
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
#ifdef QUEUE_LOG_COUNT
    fclose(queue->fp);
#endif
}

void queue_enqueue(Queue *queue, AVFrame *frame) {
    queue->buffer[queue->fill_ptr] = frame;
    queue->fill_ptr = (queue->fill_ptr + 1) % QUEUE_MAX;
    queue->count++;
#ifdef QUEUE_LOG_COUNT
    fprintf(queue->fp, "%d\n", queue->count);
#endif
}

AVFrame *queue_dequeue(Queue *queue) {
    AVFrame *frame = queue->buffer[queue->use_ptr];
    queue->use_ptr = (queue->use_ptr + 1) % QUEUE_MAX;
    queue->count--;
#ifdef QUEUE_LOG_COUNT
    fprintf(queue->fp, "%d\n", queue->count);
#endif
    return frame;
}

void queue_flush(Queue *queue) {
    for (int i = queue->use_ptr; i != queue->fill_ptr; i = (i + 1) % QUEUE_MAX) {
        av_frame_free(&queue->buffer[i]);
    }
    queue->use_ptr = queue->fill_ptr;
    queue->count = 0;
#ifdef QUEUE_LOG_COUNT
    fprintf(queue->fp, "%d\n", queue->count);
#endif
}
