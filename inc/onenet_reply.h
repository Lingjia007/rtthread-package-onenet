#ifndef _ONENET_REPLY_H_
#define _ONENET_REPLY_H_

#include <rtthread.h>
#include <cJSON.h>

#define ONENET_REPLY_SUCCESS        200
#define ONENET_REPLY_FAIL           500
#define ONENET_REPLY_NOT_FOUND      404

#define ONENET_REPLY_MSG_SUCCESS    "success"
#define ONENET_REPLY_MSG_FAIL       "fail"
#define ONENET_REPLY_MSG_NOT_FOUND  "property not found"

typedef enum
{
    ONENET_REPLY_TYPE_PROPERTY_SET,
    ONENET_REPLY_TYPE_PROPERTY_GET,
    ONENET_REPLY_TYPE_OTA_INFORM,
    ONENET_REPLY_TYPE_PROPERTY_POST_REPLY,
    ONENET_REPLY_TYPE_PROPERTY_DESIRED_GET,
    ONENET_REPLY_TYPE_PROPERTY_DESIRED_DELETE,
} onenet_reply_type_t;

typedef struct onenet_property_value
{
    const char *identifier;
    int type;
    union
    {
        int int_value;
        double double_value;
        char *string_value;
        rt_bool_t bool_value;
    } value;
} onenet_property_value_t;

typedef rt_err_t (*onenet_property_set_cb)(const char *identifier, cJSON *value);
typedef cJSON* (*onenet_property_get_cb)(const char *identifier);
typedef rt_err_t (*onenet_ota_inform_cb)(const char *version, const char *url);

void onenet_reply_init(void);

rt_uint32_t onenet_reply_get_next_id(void);

rt_err_t onenet_reply_register_set_cb(onenet_property_set_cb cb);
rt_err_t onenet_reply_register_get_cb(onenet_property_get_cb cb);
rt_err_t onenet_reply_register_ota_cb(onenet_ota_inform_cb cb);

rt_err_t onenet_reply_set_property(const char *identifier, cJSON *value);
cJSON *onenet_reply_get_property(const char *identifier);

char *onenet_reply_build_response(const char *id, int code, const char *msg);
char *onenet_reply_build_get_response(const char *id, int code, const char *msg, cJSON *params);

void onenet_reply_process(onenet_reply_type_t type, const char *topic, const uint8_t *payload, size_t payload_len,
                          uint8_t **resp_data, size_t *resp_size);

#endif
