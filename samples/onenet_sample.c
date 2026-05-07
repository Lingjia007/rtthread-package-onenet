/*
 * File      : onenet_sample.c
 * COPYRIGHT (C) 2006 - 2018, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-04-24     chenyong     first version
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <onenet.h>

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "onenet.sample"
#if ONENET_DEBUG
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif /* ONENET_DEBUG */

#include <rtdbg.h>

#ifdef FINSH_USING_MSH
#include <finsh.h>

/* upload random value to temperature*/
static void onenet_upload_entry(void *parameter)
{
    int value = 0;
    char text_buf[256];

    while (1)
    {
        value = rand() % 100;

        rt_snprintf(text_buf, sizeof(text_buf), "Temperature: %d°C, Status: Normal", value);

        if (onenet_mqtt_upload_string("TEXT", text_buf) < 0)
        {
            LOG_E("upload has an error, stop uploading");
            break;
        }
        else
        {
            LOG_D("upload TEXT data: %s", text_buf);
        }

        rt_thread_delay(rt_tick_from_millisecond(5 * 1000));
    }
}

int onenet_upload_cycle(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("onenet_send",
                           onenet_upload_entry,
                           RT_NULL,
                           2 * 1024,
                           RT_THREAD_PRIORITY_MAX / 3 - 1,
                           5);
    if (tid)
    {
        rt_thread_startup(tid);
    }

    return 0;
}
MSH_CMD_EXPORT(onenet_upload_cycle, send data to OneNET cloud cycle);

int onenet_test_text(void)
{
    char test_buf[256];
    
    rt_snprintf(test_buf, sizeof(test_buf), "Test Message: Hello OneNET! Time: %d", rt_tick_get());
    
    if (onenet_mqtt_upload_string("TEXT", test_buf) < 0)
    {
        LOG_E("upload TEXT data has an error!");
        return -1;
    }
    
    LOG_D("upload TEXT data success: %s", test_buf);
    return 0;
}
MSH_CMD_EXPORT(onenet_test_text, test upload TEXT string data to OneNET);

int onenet_publish_digit(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_E("onenet_publish [datastream_id]  [value]  - mqtt pulish digit data to OneNET.");
        return -1;
    }

    if (onenet_mqtt_upload_digit(argv[1], atoi(argv[2])) < 0)
    {
        LOG_E("upload digit data has an error!\n");
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(onenet_publish_digit, onenet_pub_digit, send digit data to onenet cloud);

int onenet_publish_string(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_E("onenet_publish [datastream_id]  [string]  - mqtt pulish string data to OneNET.");
        return -1;
    }

    if (onenet_mqtt_upload_string(argv[1], argv[2]) < 0)
    {
        LOG_E("upload string has an error!\n");
    }

    return 0;
}
MSH_CMD_EXPORT_ALIAS(onenet_publish_string, onenet_pub_string, send string data to onenet cloud);

/* onenet mqtt command response callback function */
static void onenet_cmd_rsp_cb(uint8_t *recv_data, size_t recv_size, uint8_t **resp_data, size_t *resp_size)
{
    char res_buf[] = { "cmd is received!\n" };

    LOG_D("recv data is %.*s\n", recv_size, recv_data);

    /* user have to malloc memory for response data */
    *resp_data = (uint8_t *) ONENET_MALLOC(strlen(res_buf));

    strncpy((char *)*resp_data, res_buf, strlen(res_buf));

    *resp_size = strlen(res_buf);
}

/* set the onenet mqtt command response callback function */
int onenet_set_cmd_rsp(int argc, char **argv)
{
    onenet_set_cmd_rsp_cb(onenet_cmd_rsp_cb);
    return 0;
}
MSH_CMD_EXPORT(onenet_set_cmd_rsp, set cmd response function);

static int led_status = 0;
static char firmware_version[64] = "1.0.0";
static char rtc_time[64] = "2024-01-01 00:00:00";
static char text_display[256] = "Hello OneNET";

static rt_err_t onenet_property_set_handler(const char *identifier, cJSON *value)
{
    if (strcmp(identifier, "BSP_LED") == 0)
    {
        if (cJSON_IsBool(value))
        {
            led_status = cJSON_IsTrue(value) ? 1 : 0;
            LOG_I("BSP_LED set to: %d", led_status);
            return RT_EOK;
        }
        else if (cJSON_IsNumber(value))
        {
            led_status = value->valueint;
            LOG_I("BSP_LED set to: %d", led_status);
            return RT_EOK;
        }
    }
    else if (strcmp(identifier, "FIRMWARE_VERSION") == 0)
    {
        if (cJSON_IsString(value))
        {
            strncpy(firmware_version, value->valuestring, sizeof(firmware_version) - 1);
            LOG_I("FIRMWARE_VERSION set to: %s", firmware_version);
            return RT_EOK;
        }
    }
    else if (strcmp(identifier, "RTC_TIME") == 0)
    {
        if (cJSON_IsString(value))
        {
            strncpy(rtc_time, value->valuestring, sizeof(rtc_time) - 1);
            LOG_I("RTC_TIME set to: %s", rtc_time);
            return RT_EOK;
        }
    }
    else if (strcmp(identifier, "TEXT") == 0)
    {
        if (cJSON_IsString(value))
        {
            strncpy(text_display, value->valuestring, sizeof(text_display) - 1);
            LOG_I("TEXT set to: %s", text_display);
            return RT_EOK;
        }
    }
    
    LOG_W("Unknown property or invalid type: %s", identifier);
    return -RT_ERROR;
}

static cJSON *onenet_property_get_handler(const char *identifier)
{
    cJSON *value = RT_NULL;
    
    if (identifier == RT_NULL)
    {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddBoolToObject(params, "BSP_LED", led_status ? 1 : 0);
        cJSON_AddStringToObject(params, "FIRMWARE_VERSION", firmware_version);
        cJSON_AddStringToObject(params, "RTC_TIME", rtc_time);
        cJSON_AddStringToObject(params, "TEXT", text_display);
        return params;
    }
    
    if (strcmp(identifier, "BSP_LED") == 0)
    {
        value = cJSON_CreateBool(led_status ? 1 : 0);
    }
    else if (strcmp(identifier, "FIRMWARE_VERSION") == 0)
    {
        value = cJSON_CreateString(firmware_version);
    }
    else if (strcmp(identifier, "RTC_TIME") == 0)
    {
        value = cJSON_CreateString(rtc_time);
    }
    else if (strcmp(identifier, "TEXT") == 0)
    {
        value = cJSON_CreateString(text_display);
    }
    else
    {
        LOG_W("Unknown property: %s", identifier);
    }
    
    return value;
}

static rt_err_t onenet_ota_inform_handler(const char *version, const char *url)
{
    LOG_I("OTA inform received - version: %s, url: %s", 
          version ? version : "unknown", 
          url ? url : "none");
    return RT_EOK;
}

int onenet_register_property_callbacks(void)
{
    onenet_reply_register_set_cb(onenet_property_set_handler);
    onenet_reply_register_get_cb(onenet_property_get_handler);
    onenet_reply_register_ota_cb(onenet_ota_inform_handler);
    
    LOG_I("Property callbacks registered");
    return 0;
}
MSH_CMD_EXPORT(onenet_register_property_callbacks, register property callbacks for auto reply);

int onenet_test_reply(void)
{
    char *response = RT_NULL;
    const char *test_id = "123";
    
    response = onenet_reply_build_response(test_id, ONENET_REPLY_SUCCESS, ONENET_REPLY_MSG_SUCCESS);
    if (response)
    {
        LOG_I("Test response: %s", response);
        cJSON_free(response);
    }
    
    return 0;
}
MSH_CMD_EXPORT(onenet_test_reply, test reply response builder);

#endif /* FINSH_USING_MSH */
