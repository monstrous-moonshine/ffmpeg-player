#include "app.h"
#include "macro.h"

static void reset_viewport(App *app) {
    int viewport_width, viewport_height;
    int viewport_x, viewport_y;
    viewport_width = min(app->height * app->dar.num / app->dar.den, app->width);
    viewport_height = min(app->width * app->dar.den / app->dar.num, app->height);
    viewport_x = (app->width - viewport_width) / 2;
    viewport_y = (app->height - viewport_height) / 2;
    app->viewport.h = viewport_height;
    app->viewport.w = viewport_width;
    app->viewport.x = viewport_x;
    app->viewport.y = viewport_y;
}

bool app_init(App *app, Rational *dar, SDL_AudioSpec *wanted_spec) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
        return false;
    app->win = NULL;
    app->ren = NULL;
    app->tex = NULL;
    app->done = false;
    app->resized = false;
    app->width = 640;
    app->height = 480;
    app->dar.num = dar->num;
    app->dar.den = dar->den;
    reset_viewport(app);
    app->audio_devID = SDL_OpenAudioDevice(
            NULL, 0, wanted_spec, &app->audio_spec,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
            SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (app->audio_devID == 0)
        return false;
    app->win = SDL_CreateWindow(
            "ffmpeg-player",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            app->width, app->height,
            SDL_WINDOW_RESIZABLE);
    if (!app->win)
        return false;
    app->ren = SDL_CreateRenderer(
            app->win, -1, SDL_RENDERER_ACCELERATED |
                          SDL_RENDERER_PRESENTVSYNC);
    if (!app->ren)
        return false;
    app->tex = SDL_CreateTexture(
            app->ren, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            app->width, app->height);
    if (!app->tex)
        return false;
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
}

void process_events(App *app) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            app->done = true;
            break;
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
            case SDLK_q:
                app->done = true;
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
