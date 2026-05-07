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

static void onenet_reply_handle_property_set(const uint8_t *payload, size_t payload_len,
                                             uint8_t **resp_data, size_t *resp_size)
{
    cJSON *root = RT_NULL;
    cJSON *id_item = RT_NULL;
    cJSON *params = RT_NULL;
    char *resp_str = RT_NULL;
    const char *msg_id = "0";
    int code = ONENET_REPLY_SUCCESS;
    const char *msg = ONENET_REPLY_MSG_SUCCESS;

    LOG_I("Property set request: %.*s", (int)payload_len, payload);

    root = cJSON_ParseWithLength((const char *)payload, payload_len);
    if (!root)
    {
        LOG_E("Failed to parse property set JSON");
        code = ONENET_REPLY_FAIL;
        msg = ONENET_REPLY_MSG_FAIL;
        goto build_response;
    }

    id_item = cJSON_GetObjectItem(root, "id");
    if (id_item && cJSON_IsString(id_item))
    {
        msg_id = id_item->valuestring;
    }

    params = cJSON_GetObjectItem(root, "params");
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
                code = ONENET_REPLY_FAIL;
                msg = ONENET_REPLY_MSG_FAIL;
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
        code = ONENET_REPLY_FAIL;
        msg = ONENET_REPLY_MSG_FAIL;
    }

build_response:
    resp_str = onenet_reply_build_response(msg_id, code, msg);
    if (root)
    {
        cJSON_Delete(root);
    }

    if (resp_str)
    {
        *resp_data = (uint8_t *)ONENET_MALLOC(strlen(resp_str) + 1);
        if (*resp_data)
        {
            strcpy((char *)*resp_data, resp_str);
            *resp_size = strlen(resp_str);
        }
        cJSON_free(resp_str);
    }
}

static void onenet_reply_handle_property_get(const uint8_t *payload, size_t payload_len,
                                             uint8_t **resp_data, size_t *resp_size)
{
    cJSON *root = RT_NULL;
    cJSON *id_item = RT_NULL;
    cJSON *params = RT_NULL;
    cJSON *result_params = RT_NULL;
    char *resp_str = RT_NULL;
    const char *msg_id = "0";
    int code = ONENET_REPLY_SUCCESS;
    const char *msg = ONENET_REPLY_MSG_SUCCESS;

    LOG_I("Property get request: %.*s", (int)payload_len, payload);

    root = cJSON_ParseWithLength((const char *)payload, payload_len);
    if (!root)
    {
        LOG_E("Failed to parse property get JSON");
        code = ONENET_REPLY_FAIL;
        msg = ONENET_REPLY_MSG_FAIL;
        goto build_response;
    }

    id_item = cJSON_GetObjectItem(root, "id");
    if (id_item && cJSON_IsString(id_item))
    {
        msg_id = id_item->valuestring;
    }

    result_params = cJSON_CreateObject();

    params = cJSON_GetObjectItem(root, "params");
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
    resp_str = onenet_reply_build_get_response(msg_id, code, msg, result_params);
    if (root)
    {
        cJSON_Delete(root);
    }
    if (result_params)
    {
        cJSON_Delete(result_params);
    }

    if (resp_str)
    {
        *resp_data = (uint8_t *)ONENET_MALLOC(strlen(resp_str) + 1);
        if (*resp_data)
        {
            strcpy((char *)*resp_data, resp_str);
            *resp_size = strlen(resp_str);
        }
        cJSON_free(resp_str);
    }
}

static void onenet_reply_handle_ota_inform(const uint8_t *payload, size_t payload_len,
                                           uint8_t **resp_data, size_t *resp_size)
{
    cJSON *root = RT_NULL;
    cJSON *id_item = RT_NULL;
    cJSON *version_item = RT_NULL;
    cJSON *url_item = RT_NULL;
    char *resp_str = RT_NULL;
    const char *msg_id = "0";
    int code = ONENET_REPLY_SUCCESS;
    const char *msg = ONENET_REPLY_MSG_SUCCESS;

    LOG_I("OTA inform request: %.*s", (int)payload_len, payload);

    root = cJSON_ParseWithLength((const char *)payload, payload_len);
    if (!root)
    {
        LOG_E("Failed to parse OTA inform JSON");
        code = ONENET_REPLY_FAIL;
        msg = ONENET_REPLY_MSG_FAIL;
        goto build_response;
    }

    id_item = cJSON_GetObjectItem(root, "id");
    if (id_item && cJSON_IsString(id_item))
    {
        msg_id = id_item->valuestring;
    }

    version_item = cJSON_GetObjectItem(root, "version");
    url_item = cJSON_GetObjectItem(root, "url");

    if (ota_inform_cb)
    {
        const char *version = version_item ? version_item->valuestring : RT_NULL;
        const char *url = url_item ? url_item->valuestring : RT_NULL;

        rt_err_t ret = ota_inform_cb(version, url);
        if (ret != RT_EOK)
        {
            LOG_W("OTA inform callback failed");
            code = ONENET_REPLY_FAIL;
            msg = ONENET_REPLY_MSG_FAIL;
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
    resp_str = onenet_reply_build_response(msg_id, code, msg);
    if (root)
    {
        cJSON_Delete(root);
    }

    if (resp_str)
    {
        *resp_data = (uint8_t *)ONENET_MALLOC(strlen(resp_str) + 1);
        if (*resp_data)
        {
            strcpy((char *)*resp_data, resp_str);
            *resp_size = strlen(resp_str);
        }
        cJSON_free(resp_str);
    }
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
