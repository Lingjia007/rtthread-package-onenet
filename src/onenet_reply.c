#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <onenet.h>
#include <onenet_reply.h>

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "onenet.reply"
#if ONENET_DEBUG
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif

#include <rtdbg.h>

static rt_uint32_t reply_id = 0;
static rt_mutex_t reply_mutex = RT_NULL;

static onenet_property_set_cb property_set_cb = RT_NULL;
static onenet_property_get_cb property_get_cb = RT_NULL;
static onenet_ota_inform_cb ota_inform_cb = RT_NULL;

typedef struct {
    cJSON *root;
    const char *msg_id;
    int code;
    const char *msg;
} onenet_parse_ctx_t;

void onenet_reply_init(void)
{
    if (reply_mutex == RT_NULL)
    {
        reply_mutex = rt_mutex_create("onenet_rpl", RT_IPC_FLAG_PRIO);
        if (reply_mutex == RT_NULL)
        {
            LOG_E("Failed to create reply mutex!");
        }
    }
    reply_id = 0;
    LOG_I("OneNET reply manager initialized");
}

rt_uint32_t onenet_reply_get_next_id(void)
{
    rt_uint32_t id;

    if (reply_mutex)
    {
        rt_mutex_take(reply_mutex, RT_WAITING_FOREVER);
    }

    reply_id++;
    if (reply_id == 0)
    {
        reply_id = 1;
    }
    id = reply_id;

    if (reply_mutex)
    {
        rt_mutex_release(reply_mutex);
    }

    return id;
}

rt_err_t onenet_reply_register_set_cb(onenet_property_set_cb cb)
{
    property_set_cb = cb;
    LOG_D("Property set callback registered");
    return RT_EOK;
}

rt_err_t onenet_reply_register_get_cb(onenet_property_get_cb cb)
{
    property_get_cb = cb;
    LOG_D("Property get callback registered");
    return RT_EOK;
}

rt_err_t onenet_reply_register_ota_cb(onenet_ota_inform_cb cb)
{
    ota_inform_cb = cb;
    LOG_D("OTA inform callback registered");
    return RT_EOK;
}

rt_err_t onenet_reply_set_property(const char *identifier, cJSON *value)
{
    if (property_set_cb)
    {
        return property_set_cb(identifier, value);
    }
    LOG_W("No property set callback registered for: %s", identifier);
    return -RT_ERROR;
}

cJSON *onenet_reply_get_property(const char *identifier)
{
    if (property_get_cb)
    {
        return property_get_cb(identifier);
    }
    LOG_W("No property get callback registered for: %s", identifier);
    return RT_NULL;
}

char *onenet_reply_build_response(const char *id, int code, const char *msg)
{
    cJSON *root = RT_NULL;
    char *resp_str = RT_NULL;

    root = cJSON_CreateObject();
    if (!root)
    {
        LOG_E("Failed to create JSON object for response");
        return RT_NULL;
    }

    cJSON_AddStringToObject(root, "id", id ? id : "0");
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", msg);

    resp_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return resp_str;
}

char *onenet_reply_build_get_response(const char *id, int code, const char *msg, cJSON *params)
{
    cJSON *root = RT_NULL;
    char *resp_str = RT_NULL;

    root = cJSON_CreateObject();
    if (!root)
    {
        LOG_E("Failed to create JSON object for get response");
        return RT_NULL;
    }

    cJSON_AddStringToObject(root, "id", id ? id : "0");
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", msg);

    if (params && cJSON_IsObject(params))
    {
        cJSON_AddItemToObject(root, "data", cJSON_Duplicate(params, 1));
    }

    resp_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return resp_str;
}

static rt_bool_t onenet_parse_request(const uint8_t *payload, size_t payload_len,
                                       onenet_parse_ctx_t *ctx, const char *type_name)
{
    ctx->root = cJSON_ParseWithLength((const char *)payload, payload_len);
    if (!ctx->root)
    {
        LOG_E("Failed to parse %s JSON", type_name);
        ctx->code = ONENET_REPLY_FAIL;
        ctx->msg = ONENET_REPLY_MSG_FAIL;
        ctx->msg_id = "0";
        return RT_FALSE;
    }

    cJSON *id_item = cJSON_GetObjectItem(ctx->root, "id");
    ctx->msg_id = (id_item && cJSON_IsString(id_item)) ? id_item->valuestring : "0";
    ctx->code = ONENET_REPLY_SUCCESS;
    ctx->msg = ONENET_REPLY_MSG_SUCCESS;

    return RT_TRUE;
}

static uint8_t *onenet_build_response_buffer(const char *msg_id, int code, const char *msg,
                                              cJSON *data, size_t *resp_size)
{
    char *resp_str = RT_NULL;
    uint8_t *resp_data = RT_NULL;

    if (data)
    {
        resp_str = onenet_reply_build_get_response(msg_id, code, msg, data);
    }
    else
    {
        resp_str = onenet_reply_build_response(msg_id, code, msg);
    }

    if (resp_str)
    {
        resp_data = (uint8_t *)ONENET_MALLOC(strlen(resp_str) + 1);
        if (resp_data)
        {
            strcpy((char *)resp_data, resp_str);
            *resp_size = strlen(resp_str);
        }
        cJSON_free(resp_str);
    }

    return resp_data;
}

static void onenet_reply_handle_property_set(const uint8_t *payload, size_t payload_len,
                                             uint8_t **resp_data, size_t *resp_size)
{
    onenet_parse_ctx_t ctx = {0};
    cJSON *params = RT_NULL;

    LOG_I("Property set request: %.*s", (int)payload_len, payload);

    if (!onenet_parse_request(payload, payload_len, &ctx, "property set"))
    {
        goto build_response;
    }

    params = cJSON_GetObjectItem(ctx.root, "params");
    if (params && cJSON_IsObject(params))
    {
        cJSON *item = RT_NULL;
        cJSON_ArrayForEach(item, params)
        {
            const char *identifier = item->string;
            rt_err_t ret = onenet_reply_set_property(identifier, item);

            if (ret != RT_EOK)
            {
                LOG_W("Failed to set property: %s", identifier);
                ctx.code = ONENET_REPLY_FAIL;
                ctx.msg = ONENET_REPLY_MSG_FAIL;
            }
            else
            {
                LOG_I("Property set success: %s", identifier);
            }
        }
    }
    else
    {
        LOG_E("No params in property set message");
        ctx.code = ONENET_REPLY_FAIL;
        ctx.msg = ONENET_REPLY_MSG_FAIL;
    }

build_response:
    *resp_data = onenet_build_response_buffer(ctx.msg_id, ctx.code, ctx.msg, RT_NULL, resp_size);
    if (ctx.root) cJSON_Delete(ctx.root);
}

static void onenet_reply_handle_property_get(const uint8_t *payload, size_t payload_len,
                                             uint8_t **resp_data, size_t *resp_size)
{
    onenet_parse_ctx_t ctx = {0};
    cJSON *params = RT_NULL;
    cJSON *result_params = RT_NULL;

    LOG_I("Property get request: %.*s", (int)payload_len, payload);

    if (!onenet_parse_request(payload, payload_len, &ctx, "property get"))
    {
        goto build_response;
    }

    result_params = cJSON_CreateObject();

    params = cJSON_GetObjectItem(ctx.root, "params");
    if (params && cJSON_IsArray(params))
    {
        cJSON *item = RT_NULL;
        cJSON_ArrayForEach(item, params)
        {
            if (cJSON_IsString(item))
            {
                const char *identifier = item->valuestring;
                cJSON *value = onenet_reply_get_property(identifier);
                if (value)
                {
                    cJSON_AddItemToObject(result_params, identifier, value);
                }
                else
                {
                    LOG_W("Property not found: %s", identifier);
                }
            }
        }
    }
    else
    {
        cJSON *value = onenet_reply_get_property(RT_NULL);
        if (value && cJSON_IsObject(value))
        {
            cJSON_Delete(result_params);
            result_params = value;
        }
    }

build_response:
    *resp_data = onenet_build_response_buffer(ctx.msg_id, ctx.code, ctx.msg, result_params, resp_size);
    if (ctx.root) cJSON_Delete(ctx.root);
    if (result_params) cJSON_Delete(result_params);
}

static void onenet_reply_handle_ota_inform(const uint8_t *payload, size_t payload_len,
                                           uint8_t **resp_data, size_t *resp_size)
{
    onenet_parse_ctx_t ctx = {0};
    cJSON *version_item = RT_NULL;
    cJSON *url_item = RT_NULL;

    LOG_I("OTA inform request: %.*s", (int)payload_len, payload);

    if (!onenet_parse_request(payload, payload_len, &ctx, "OTA inform"))
    {
        goto build_response;
    }

    version_item = cJSON_GetObjectItem(ctx.root, "version");
    url_item = cJSON_GetObjectItem(ctx.root, "url");

    if (ota_inform_cb)
    {
        const char *version = version_item ? version_item->valuestring : RT_NULL;
        const char *url = url_item ? url_item->valuestring : RT_NULL;

        rt_err_t ret = ota_inform_cb(version, url);
        if (ret != RT_EOK)
        {
            LOG_W("OTA inform callback failed");
            ctx.code = ONENET_REPLY_FAIL;
            ctx.msg = ONENET_REPLY_MSG_FAIL;
        }
        else
        {
            LOG_I("OTA inform processed: version=%s", version ? version : "unknown");
        }
    }
    else
    {
        LOG_W("No OTA inform callback registered");
    }

build_response:
    *resp_data = onenet_build_response_buffer(ctx.msg_id, ctx.code, ctx.msg, RT_NULL, resp_size);
    if (ctx.root) cJSON_Delete(ctx.root);
}

void onenet_reply_process(onenet_reply_type_t type, const char *topic, const uint8_t *payload, size_t payload_len,
                          uint8_t **resp_data, size_t *resp_size)
{
    LOG_D("Processing reply type: %d, topic: %s", type, topic ? topic : "NULL");
    LOG_D("Payload: %.*s", (int)payload_len, payload);

    *resp_data = RT_NULL;
    *resp_size = 0;

    switch (type)
    {
    case ONENET_REPLY_TYPE_PROPERTY_SET:
        onenet_reply_handle_property_set(payload, payload_len, resp_data, resp_size);
        break;

    case ONENET_REPLY_TYPE_PROPERTY_GET:
        onenet_reply_handle_property_get(payload, payload_len, resp_data, resp_size);
        break;

    case ONENET_REPLY_TYPE_OTA_INFORM:
        onenet_reply_handle_ota_inform(payload, payload_len, resp_data, resp_size);
        break;

    default:
        LOG_W("Unknown reply type: %d", type);
        break;
    }

    if (*resp_data)
    {
        LOG_D("Response: %.*s", (int)*resp_size, *resp_data);
    }
}

rt_bool_t onenet_parse_value(cJSON *json, onenet_property_value_t *prop)
{
    if (!json || !prop)
    {
        return RT_FALSE;
    }

    switch (prop->type)
    {
    case ONENET_TYPE_INT32:
        if (cJSON_IsNumber(json))
        {
            prop->value.int32_value = (rt_int32_t)json->valueint;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_INT64:
        if (cJSON_IsNumber(json))
        {
            prop->value.int64_value = (rt_int64_t)json->valuedouble;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_FLOAT:
        if (cJSON_IsNumber(json))
        {
            prop->value.float_value = (float)json->valuedouble;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_DOUBLE:
        if (cJSON_IsNumber(json))
        {
            prop->value.double_value = json->valuedouble;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_DATE:
        if (cJSON_IsNumber(json))
        {
            prop->value.date_value = (rt_int64_t)json->valuedouble;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_BOOL:
        if (cJSON_IsBool(json))
        {
            prop->value.bool_value = cJSON_IsTrue(json) ? RT_TRUE : RT_FALSE;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_STRING:
        if (cJSON_IsString(json))
        {
            prop->value.string_value = json->valuestring;
            return RT_TRUE;
        }
        break;

    case ONENET_TYPE_ENUM:
        if (cJSON_IsNumber(json))
        {
            prop->value.enum_value = (rt_int32_t)json->valueint;
            return RT_TRUE;
        }
        break;

    default:
        LOG_W("Unknown property type: %d", prop->type);
        break;
    }

    LOG_W("Failed to parse value for type %d", prop->type);
    return RT_FALSE;
}

cJSON *onenet_create_value(onenet_property_value_t *prop)
{
    cJSON *value = RT_NULL;

    if (!prop)
    {
        return RT_NULL;
    }

    switch (prop->type)
    {
    case ONENET_TYPE_INT32:
        value = cJSON_CreateNumber((double)prop->value.int32_value);
        break;

    case ONENET_TYPE_INT64:
        value = cJSON_CreateNumber((double)prop->value.int64_value);
        break;

    case ONENET_TYPE_FLOAT:
        value = cJSON_CreateNumber((double)prop->value.float_value);
        break;

    case ONENET_TYPE_DOUBLE:
        value = cJSON_CreateNumber(prop->value.double_value);
        break;

    case ONENET_TYPE_DATE:
        value = cJSON_CreateNumber((double)prop->value.date_value);
        break;

    case ONENET_TYPE_BOOL:
        value = cJSON_CreateBool(prop->value.bool_value);
        break;

    case ONENET_TYPE_STRING:
        value = cJSON_CreateString(prop->value.string_value);
        break;

    case ONENET_TYPE_ENUM:
        value = cJSON_CreateNumber((double)prop->value.enum_value);
        break;

    default:
        LOG_W("Unknown property type: %d", prop->type);
        break;
    }

    return value;
}
