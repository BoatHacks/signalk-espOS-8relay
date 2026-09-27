#include "tone.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

size_t tone_library_parse(const char *json, tone_t *out, size_t max)
{
    if (!json) {
        return 0;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return 0;
    }
    size_t n = 0;
    if (cJSON_IsArray(root)) {
        const cJSON *row;
        cJSON_ArrayForEach(row, root)
        {
            if (n >= max || !cJSON_IsObject(row)) {
                continue;
            }
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(row, "name");
            const cJSON *rtttl = cJSON_GetObjectItemCaseSensitive(row, "rtttl");
            if (!cJSON_IsString(name) || !cJSON_IsString(rtttl) || name->valuestring[0] == '\0' ||
                strlen(name->valuestring) > TONE_NAME_MAX) {
                continue;
            }
            tone_t tone = {0};
            tone.n_notes = rtttl_parse(rtttl->valuestring, tone.notes, RTTTL_MAX_NOTES);
            if (tone.n_notes == 0) {
                continue;
            }
            snprintf(tone.name, sizeof(tone.name), "%s", name->valuestring);
            out[n++] = tone;
        }
    }
    cJSON_Delete(root);
    return n;
}

int tone_library_find(const tone_t *tones, size_t n, const char *name)
{
    if (!name || name[0] == '\0') {
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        if (strcmp(tones[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool tone_priority_allowed(tone_priority_t requested, tone_priority_t active)
{
    return requested > active;
}
