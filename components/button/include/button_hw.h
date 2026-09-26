// GPIO glue for the BOOT button. Device builds only.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool (*read)(void *ctx);  // true = pressed
    void *ctx;
} button_hw_t;

esp_err_t button_hw_create(button_hw_t *out);

#ifdef __cplusplus
}
#endif
