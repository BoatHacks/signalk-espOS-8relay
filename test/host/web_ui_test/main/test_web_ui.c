#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "unity.h"
#include "web_ui_logic.h"

static device_config_t cfg;

static void fresh(void)
{
    memset(&cfg, 0, sizeof(cfg));
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        snprintf(cfg.relays[i].name, sizeof(cfg.relays[i].name), "Relay %d", i + 1);
        snprintf(cfg.inputs[i].name, sizeof(cfg.inputs[i].name), "Input %d", i + 1);
    }
}

static cJSON *state(uint8_t relays, uint8_t inputs, bool ready)
{
    const web_ui_view_t v = {.cfg = &cfg, .relay_mask = relays, .input_mask = inputs, .inputs_ready = ready};
    char *json = web_ui_state_json(&v);
    TEST_ASSERT_NOT_NULL(json);
    cJSON *root = cJSON_Parse(json);
    free(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "not valid JSON");
    return root;
}

static cJSON *item(cJSON *root, const char *array, int index)
{
    cJSON *a = cJSON_GetObjectItem(root, array);
    TEST_ASSERT_EQUAL(BOARD_CHANNELS, cJSON_GetArraySize(a));
    return cJSON_GetArrayItem(a, index);
}

TEST_CASE("state: relay and input bits map to channels 1-8", "[web_ui]")
{
    fresh();
    cJSON *root = state(0x81, 0x02, true);  // relays 1 and 8, input 2
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "relays", 0), "on")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "relays", 1), "on")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "relays", 7), "on")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "inputs", 0), "on")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(item(root, "inputs", 1), "on")));
    TEST_ASSERT_EQUAL(8, cJSON_GetObjectItem(item(root, "relays", 7), "channel")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(root, "inputsReady")));
    cJSON_Delete(root);
}

TEST_CASE("state: inputs are null until they have settled", "[web_ui]")
{
    fresh();
    cJSON *root = state(0, 0xFF, false);
    for (int i = 0; i < BOARD_CHANNELS; i++) {
        TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(item(root, "inputs", i), "on")));
    }
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(root, "inputsReady")));
    cJSON_Delete(root);
}

TEST_CASE("state: names are escaped; mode and input link reported", "[web_ui]")
{
    fresh();
    strcpy(cfg.relays[2].name, "Deck \"light\" \\ <b>");
    cfg.relays[2].mode = RELAY_MODE_MOMENTARY;
    cfg.relays[2].override_di = 5;
    cfg.relays[2].link = INPUT_LINK_TOGGLE;
    cfg.relays[2].max_on_s = 600;  // momentary: reported as no limit
    cfg.relays[1].max_on_s = 1800;
    cJSON *root = state(0, 0, true);
    cJSON *r = item(root, "relays", 2);
    TEST_ASSERT_EQUAL_STRING("Deck \"light\" \\ <b>", cJSON_GetObjectItem(r, "name")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(r, "momentary")));
    TEST_ASSERT_EQUAL(5, cJSON_GetObjectItem(r, "input")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(r, "inputToggle")));
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(r, "maxOnS")->valueint);
    TEST_ASSERT_EQUAL(1800, cJSON_GetObjectItem(item(root, "relays", 1), "maxOnS")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "relays", 1), "inputToggle")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(item(root, "relays", 0), "momentary")));
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(item(root, "relays", 0), "input")->valueint);
    cJSON_Delete(root);
}

TEST_CASE("channel from path: 1-8 only", "[web_ui]")
{
    const char *p = "/api/v1/relays";
    TEST_ASSERT_EQUAL(1, web_ui_parse_channel("/api/v1/relays/1", p));
    TEST_ASSERT_EQUAL(8, web_ui_parse_channel("/api/v1/relays/8", p));
    TEST_ASSERT_EQUAL(3, web_ui_parse_channel("/api/v1/relays/3?x=1", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/0", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/9", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/10", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/1x", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays/", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relays", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/api/v1/relaysX/1", p));
    TEST_ASSERT_EQUAL(0, web_ui_parse_channel("/other/1", p));
}

TEST_CASE("body: only a boolean \"on\" is accepted", "[web_ui]")
{
    bool on = false;
    TEST_ASSERT_TRUE(web_ui_parse_on("{\"on\":true}", &on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(web_ui_parse_on("{ \"on\" : false, \"x\": 1 }", &on));
    TEST_ASSERT_FALSE(on);
    on = true;
    TEST_ASSERT_FALSE(web_ui_parse_on("{\"on\":1}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("{\"on\":\"true\"}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("{\"On\":true}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("{}", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("not json", &on));
    TEST_ASSERT_FALSE(web_ui_parse_on("", &on));
    TEST_ASSERT_TRUE(on);  // untouched by the failures
}
