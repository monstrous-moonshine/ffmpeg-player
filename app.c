#include <assert.h>
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>
#include "app.h"
#include "macro.h"
#include "param.h"

extern thread_param_t thread_params;

static void reset_viewport(App *app) {
    int viewport_width, viewport_height;
    int viewport_x, viewport_y;
    int adj_width, adj_height;
    adj_width = app->height * app->display_aspect.num /
        app->display_aspect.den;
    adj_height = app->width * app->display_aspect.den /
        app->display_aspect.num;
    viewport_width = min(adj_width, app->width);
    viewport_height = min(adj_height, app->height);
    viewport_x = (app->width - viewport_width) / 2;
    viewport_y = (app->height - viewport_height) / 2;
    app->viewport.h = viewport_height;
    app->viewport.w = viewport_width;
    app->viewport.x = viewport_x;
    app->viewport.y = viewport_y;
}

bool app_init(App *app,
        SDL_AudioSpec *wanted_spec,
        Rational *display_aspect) {
    app->t_start = -1;
    app->pts = 0;
    app->paused = false;

    app->win = NULL;
    app->ren = NULL;
    app->tex = NULL;
    app->display_aspect.num = display_aspect->num;
    app->display_aspect.den = display_aspect->den;

    app->volume = 1.0;
    app->muted = false;

    app->width = 640;
    app->height = 480;
    app->resized = false;
    app->done = false;

    reset_viewport(app);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "Error initializing SDL\n");
        return false;
    }

    app->audio_devID = SDL_OpenAudioDevice(
            NULL, 0, wanted_spec, &app->audio_spec,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
            SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (app->audio_devID == 0) {
        fprintf(stderr, "Error opening audio device\n");
        return false;
    }

    app->win = SDL_CreateWindow(
            "ffmpeg-player",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            app->width, app->height,
            SDL_WINDOW_RESIZABLE);
    if (!app->win) {
        fprintf(stderr, "Error creating window\n");
        return false;
    }

    app->ren = SDL_CreateRenderer(
            app->win, -1, SDL_RENDERER_ACCELERATED |
                          SDL_RENDERER_PRESENTVSYNC);
    if (!app->ren) {
        fprintf(stderr, "Error creating renderer\n");
        return false;
    }

    app->tex = SDL_CreateTexture(
            app->ren, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            app->width, app->height);
    if (!app->tex) {
        fprintf(stderr, "Error creating texture\n");
        return false;
    }

    SDL_PauseAudioDevice(app->audio_devID, 0);

    return true;
}

void app_fini(App *app) {
    if (app->tex) {
        SDL_DestroyTexture(app->tex);
        app->tex = NULL;
    }
    if (app->ren) {
        SDL_DestroyRenderer(app->ren);
        app->ren = NULL;
    }
    if (app->win) {
        SDL_DestroyWindow(app->win);
        app->win = NULL;
    }
    SDL_CloseAudioDevice(app->audio_devID);
    SDL_Quit();
}

static void seek(App *app, int delta) {
    thread_params.seek_flags = delta < 0 ? AVSEEK_FLAG_BACKWARD : 0;
    thread_params.seek_pts = app->pts + delta;

    assert(SDL_LockMutex(thread_params.seek_mtx) == 0);
    thread_params.do_seek = true;
    while (thread_params.do_seek) {
        assert(SDL_CondWait(thread_params.seek_done,
                    thread_params.seek_mtx) == 0);
    }
    assert(SDL_UnlockMutex(thread_params.seek_mtx) == 0);

    SDL_ClearQueuedAudio(app->audio_devID);
    app->t_start = -1;
}

static void toggle_pause(App *app) {
    if (app->paused) {
        app->paused = false;
        SDL_PauseAudioDevice(app->audio_devID, 0);
        app->t_start = -1;
    } else {
        app->paused = true;
        SDL_PauseAudioDevice(app->audio_devID, 1);
    }
}

void process_events(App *app) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            app->done = true;
            thread_params.done = true;
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_q:
                app->done = true;
                thread_params.done = true;
                break;
            case SDLK_SPACE:
                toggle_pause(app);
                break;
            case SDLK_m:
                app->muted = !app->muted;
                break;
            case SDLK_9:
                app->volume = max(app->volume - 0.1f, 0.0f);
                break;
            case SDLK_0:
                app->volume = min(app->volume + 0.1f, 1.0f);
                break;
            case SDLK_RIGHT:
                seek(app, 10000);
                break;
            case SDLK_LEFT:
                seek(app, -10000);
                break;
            case SDLK_UP:
                seek(app, 60000);
                break;
            case SDLK_DOWN:
                seek(app, -60000);
                break;
            }
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                app->width = e.window.data1;
                app->height = e.window.data2;
                app->resized = true;
                reset_viewport(app);
            }
            break;
        default:
            break;
        }
    }
}
