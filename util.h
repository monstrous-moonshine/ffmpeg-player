#pragma once
#include <libavutil/error.h>

static inline const char *my_avstrerror(int err) {
    static char buffer[AV_ERROR_MAX_STRING_SIZE];
    (void)av_strerror(err, buffer, AV_ERROR_MAX_STRING_SIZE);
    return buffer;
}
