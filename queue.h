#pragma once
#include <stdbool.h>

#define QUEUE_MAX 8

typedef struct AVFrame AVFrame;
typedef struct SDL_cond SDL_cond;
typedef struct SDL_mutex SDL_mutex;

typedef struct {
    AVFrame *frame;
    int stream_index;
} entry_t;

typedef struct {
    int count;
    int fill_ptr, use_ptr;
    entry_t buffer[QUEUE_MAX];
    SDL_cond *empty, *fill;
    SDL_mutex *mutex;
} Queue;

bool queue_init(Queue *queue);
void queue_fini(Queue *queue);
void queue_enqueue(Queue *queue, entry_t entry);
entry_t queue_dequeue(Queue *queue);
void queue_flush(Queue *queue);
