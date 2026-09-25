#include "sk_espos.h"

#include "espos_sk.h"

static esp_err_t put_register(const char *path, sk_put_handler_t cb, void *arg)
{
    return espos_sk_put_handler_register(path, cb, arg);
}

const sk_api_t sk_espos_api = {
    .publish_number = espos_sk_publish_number,
    .publish_string = espos_sk_publish_string,
    .declare_meta = espos_sk_declare_meta,
    .put_register = put_register,
};
