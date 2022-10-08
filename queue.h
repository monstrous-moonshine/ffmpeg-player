#pragma once
#include <stdbool.h>
#include <stdio.h>

/* queue size must be big enough that the audio
 * queue doesn't clog up and keep the video from
 * coming */
#define QUEUE_MAX 16

typedef struct AVFrame AVFrame;
typedef struct SDL_cond SDL_cond;
typedef struct SDL_mutex SDL_mutex;

typedef struct {
    int count;
    int fill_ptr, use_ptr;
    AVFrame *buffer[QUEUE_MAX];
    SDL_cond *empty, *fill;
    SDL_mutex *mutex;
#ifdef QUEUE_LOG_COUNT
    FILE *fp;
#endif
} Queue;

bool queue_init(Queue *queue
#ifdef QUEUE_LOG_COUNT
        , const char *name
#endif
        );
void queue_fini(Queue *queue);
void queue_enqueue(Queue *queue, AVFrame *frame);
AVFrame *queue_dequeue(Queue *queue);
void queue_flush(Queue *queue);
