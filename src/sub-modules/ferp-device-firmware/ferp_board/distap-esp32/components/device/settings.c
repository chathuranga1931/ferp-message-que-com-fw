#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "string.h"
#include "esp_log.h"
#include "spiff_mount.h"
#include "settings.h"

#define SETTINGS_BUFFER_MAX 256

static const char *system_storage_key = "system_settings.txt";
// static const char *TAG = "system_settings";

settings_t settings = {};

bool exist_system_settings(void)
{
    return local_storage_file_exist(system_storage_key);
}

esp_err_t load_system_settings(void)
{
    esp_err_t ret = ESP_OK;
    size_t readlen = SETTINGS_BUFFER_MAX;

    char *readbuf = (char *)malloc(SETTINGS_BUFFER_MAX);
    if (NULL == readbuf)
    {
        // ESP_LOGE(TAG, "settings string malloc fail.");
        ret = ESP_ERR_NO_MEM;
        goto end;
    }

    /* Try read activate config data */
    ret = local_storage_get((const char *)system_storage_key, (uint8_t *)readbuf, &readlen);
    if (ESP_OK != ret)
    {
        // ESP_LOGI(TAG, "activate config not found:%d", ret);
        goto end_buff;
    }

    // ESP_LOGI(TAG, "reading %d - %s", readlen, readbuf);
    // raw_print(readbuf);

    cJSON *root = cJSON_Parse(readbuf);
    if (NULL == root)
    {
        ret = ESP_ERR_INVALID_ARG;
        goto end_buff;
    }

    if (cJSON_GetObjectItem(root, "display") == NULL || cJSON_GetObjectItem(root, "error_mask") == NULL)
    {
        ret = ESP_ERR_INVALID_ARG;
        goto end_json;
    }
    settings.display = (display_type_t)cJSON_GetObjectItem(root, "display")->valueint;
    settings.error_mask.u8int = (uint8_t)cJSON_GetObjectItem(root, "error_mask")->valueint;
    // printf("display:%d, error_mask:0x%.2x\r\n", settings.display, settings.error_mask.u8int);

end_json:
    cJSON_Delete(root);
end_buff:
    free(readbuf);
end:
    return ret;
}

esp_err_t save_system_settings(void)
{
    esp_err_t ret = ESP_OK;
    size_t buff_len = 0;
    char *writebuf;
    writebuf = (char *)malloc(SETTINGS_BUFFER_MAX);
    if (writebuf == NULL)
    {
        // ESP_LOGE(TAG, "save string malloc fail.");
        ret = ESP_ERR_NO_MEM;
        goto end;
    }

    buff_len = sprintf(writebuf, "{\"display\":%d, \"error_mask\":%d}", settings.display, settings.error_mask.u8int);
    // printf("saving:%d\r\n%s\r\n", buff_len, writebuf);

    /* Write config data */
    ret = local_storage_set(system_storage_key, (const uint8_t *)writebuf, buff_len); // writing with null charactor
    if (ret != ESP_OK)
    {
        // ESP_LOGE(TAG, "saving error:0x%02x", ret);
        ret = ESP_FAIL;
        goto end_buff;
    }
end_buff:
    free((void *)writebuf);
end:
    return ret;
}

void load_default_system_settings()
{
    settings.display = DIS_NONE;
    settings.error_mask = (const data_error_t){
        .bits = {
            .index = true,
            .price_gap = false, //price gap removed by default
            .totprice = true,
            .unitprice = true,
            .volume = true,
        }
    };
    save_system_settings();
    // printf("DEFAULT Settings loaded...\r\n");
}

void init_system_settings()
{
    if(exist_system_settings())
    {
        const esp_err_t ret = load_system_settings();
        if(ret != ESP_OK)
        {
            load_default_system_settings();
        }
    }    
    else
    {
        load_default_system_settings();
    }
}