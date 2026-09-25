// The board's input pins and the system clock behind input_sense. Device
// builds only.
#pragma once

#include "esp_err.h"
#include "input_sense.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t input_hw_create(input_sense_hw_t *out);

#ifdef __cplusplus
}
#endif
