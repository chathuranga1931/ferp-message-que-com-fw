/**
 * @file pal_esp_idf_fw_update.cpp
 * @brief Platform Abstraction Layer - ESP-IDF Firmware Update Implementation
 *
 * Implements pal_fw_update.h using the ESP-IDF OTA API (esp_ota_ops.h).
 * Maps the platform-independent session lifecycle to:
 *   esp_ota_begin()  → pal_fw_update_begin()
 *   esp_ota_write()  → pal_fw_update_write()
 *   esp_ota_end() + esp_ota_set_boot_partition() → pal_fw_update_end()
 *   esp_ota_abort()  → pal_fw_update_abort()
 */

#include "pal_fw_update.h"
#include "pal/pal_logger.h"

#include <string.h>
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"

#define __TAG__ "PAL_FWUP"

#define FWUP_ERROR_LOG_EN      LOG_DIS
#define FWUP_INFO_LOG_EN       LOG_DIS
#define FWUP_DEBUG_LOG_EN      LOG_DIS

/*===========================================================================*/
/*                         INTERNAL SESSION STATE                            */
/*===========================================================================*/

/**
 * @brief Internal session context.
 *
 * A single global instance is sufficient because only one OTA session may
 * be active at a time (enforced by begin()).
 */
typedef struct {
    esp_ota_handle_t       esp_handle;       ///< ESP-IDF OTA handle
    const esp_partition_t* partition;        ///< Target OTA partition
    bool                   is_open;          ///< True while a session is active
    pal_fw_update_status_t status;           ///< Externally visible status snapshot
} fw_update_ctx_t;

static fw_update_ctx_t s_ctx = {
    .esp_handle = 0,
    .partition  = NULL,
    .is_open    = false,
    .status = {
        .state         = PAL_FW_UPDATE_STATE_IDLE,
        .bytes_written = 0,
        .error_code    = PAL_OK,
        .status_str    = "Idle",
    }
};

/*===========================================================================*/
/*                         INTERNAL HELPERS                                  */
/*===========================================================================*/

/**
 * @brief Update the human-readable status string and error fields in one call.
 */
static void _set_status(pal_fw_update_state_t state, int32_t error_code,
                        const char* fmt, ...)
{
    s_ctx.status.state      = state;
    s_ctx.status.error_code = error_code;

    va_list args;
    va_start(args, fmt);
    vsnprintf(s_ctx.status.status_str, sizeof(s_ctx.status.status_str), fmt, args);
    va_end(args);
}

/*===========================================================================*/
/*                         API IMPLEMENTATION                                */
/*===========================================================================*/

int32_t pal_fw_update_begin(pal_fw_update_handle_t* handle)
{
    if(handle == NULL) {
        return PAL_ERROR_INVALID;
    }

    *handle = NULL;

    if(s_ctx.is_open) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "OTA session already in progress");
        _set_status(PAL_FW_UPDATE_STATE_IN_PROGRESS, PAL_ERROR_BUSY, "Already in progress");
        return PAL_ERROR_BUSY;
    }

    /* Select the next OTA slot */
    s_ctx.partition = esp_ota_get_next_update_partition(NULL);
    if(s_ctx.partition == NULL) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "No OTA update partition found");
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR_NOT_FOUND, "No OTA partition");
        return PAL_ERROR_NOT_FOUND;
    }

    LOG_MSG_INFO(FWUP_INFO_LOG_EN, "OTA target partition: subtype %d at offset 0x%08x",
                 s_ctx.partition->subtype, s_ctx.partition->address);

    esp_err_t err = esp_ota_begin(s_ctx.partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ctx.esp_handle);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "esp_ota_begin failed: %s", esp_err_to_name(err));
        s_ctx.partition = NULL;
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR_INIT,
                    "Begin failed: %s", esp_err_to_name(err));
        return PAL_ERROR_INIT;
    }

    s_ctx.is_open              = true;
    s_ctx.status.bytes_written = 0;
    _set_status(PAL_FW_UPDATE_STATE_IN_PROGRESS, PAL_OK, "In progress");

    LOG_MSG_DEBUG(FWUP_DEBUG_LOG_EN, "OTA session started");
    *handle = (pal_fw_update_handle_t)&s_ctx;

    return PAL_OK;
}

int32_t pal_fw_update_write(pal_fw_update_handle_t handle,
                            const uint8_t*         data,
                            size_t                 len)
{
    if(handle == NULL || data == NULL || len == 0) {
        return PAL_ERROR_INVALID;
    }

    if(!s_ctx.is_open) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "pal_fw_update_write called with no active session");
        return PAL_ERROR_INVALID;
    }

    esp_err_t err = esp_ota_write(s_ctx.esp_handle, data, len);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "esp_ota_write failed: %s", esp_err_to_name(err));
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR_IO,
                    "Write failed: %s", esp_err_to_name(err));
        return PAL_ERROR_IO;
    }

    s_ctx.status.bytes_written += (uint32_t)len;
    _set_status(PAL_FW_UPDATE_STATE_IN_PROGRESS, PAL_OK,
                "In progress: %lu bytes", (unsigned long)s_ctx.status.bytes_written);

    return PAL_OK;
}

int32_t pal_fw_update_end(pal_fw_update_handle_t handle)
{
    if(handle == NULL) {
        return PAL_ERROR_INVALID;
    }

    if(!s_ctx.is_open) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "pal_fw_update_end called with no active session");
        return PAL_ERROR_INVALID;
    }

    /* Session is now closed regardless of outcome */
    s_ctx.is_open = false;

    /* Validate the written image */
    esp_err_t err = esp_ota_end(s_ctx.esp_handle);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "esp_ota_end (image validation) failed: %s", esp_err_to_name(err));
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR,
                    "Validation failed: %s", esp_err_to_name(err));
        return PAL_ERROR;
    }

    /* Commit as the next boot target */
    err = esp_ota_set_boot_partition(s_ctx.partition);
    if(err != ESP_OK) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR_INIT,
                    "Boot partition set failed: %s", esp_err_to_name(err));
        return PAL_ERROR_INIT;
    }

    LOG_MSG_INFO(FWUP_INFO_LOG_EN, "OTA complete: %lu bytes written, boot partition committed",
                 (unsigned long)s_ctx.status.bytes_written);
    _set_status(PAL_FW_UPDATE_STATE_SUCCESS, PAL_OK,
                "Success: %lu bytes", (unsigned long)s_ctx.status.bytes_written);

    return PAL_OK;
}

int32_t pal_fw_update_abort(pal_fw_update_handle_t handle)
{
    if(handle == NULL) {
        return PAL_ERROR_INVALID;
    }

    if(!s_ctx.is_open) {
        /* Already closed — nothing to abort */
        return PAL_OK;
    }

    esp_err_t err = esp_ota_abort(s_ctx.esp_handle);
    s_ctx.is_open   = false;
    s_ctx.partition = NULL;

    if(err != ESP_OK) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "esp_ota_abort failed: %s", esp_err_to_name(err));
        _set_status(PAL_FW_UPDATE_STATE_ABORTED, PAL_ERROR,
                    "Abort error: %s", esp_err_to_name(err));
        return PAL_ERROR;
    }

    LOG_MSG_DEBUG(FWUP_DEBUG_LOG_EN, "OTA session aborted");
    _set_status(PAL_FW_UPDATE_STATE_ABORTED, PAL_OK, "Aborted");

    return PAL_OK;
}

int32_t pal_fw_update_get_status(pal_fw_update_status_t* status)
{
    if(status == NULL) {
        return PAL_ERROR_INVALID;
    }

    memcpy(status, &s_ctx.status, sizeof(pal_fw_update_status_t));
    return PAL_OK;
}

/*===========================================================================*/
/*                         BINARY INFO QUERIES                               */
/*===========================================================================*/

int32_t pal_fw_update_get_bin_info(pal_bin_slot_t slot, pal_bin_info_t* info)
{
    if(info == NULL) {
        return PAL_ERROR_INVALID;
    }

    memset(info, 0, sizeof(pal_bin_info_t));
    info->valid = false;

    const esp_partition_t*  partition = NULL;
    const esp_app_desc_t*   desc      = NULL;

    if(slot == PAL_BIN_SLOT_RUNNING)
    {
        /* Running partition — esp_app_get_description() is always available */
        desc = esp_app_get_description();
        if(desc == NULL) {
            LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "esp_app_get_description() returned NULL");
            return PAL_ERROR_NOT_FOUND;
        }
    }
    else /* PAL_BIN_SLOT_NEXT */
    {
        partition = esp_ota_get_next_update_partition(NULL);
        if(partition == NULL) {
            LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "No next OTA partition found");
            return PAL_ERROR_NOT_FOUND;
        }

        /* Read descriptor from flash — safe, read-only */
        esp_app_desc_t next_desc;
        esp_err_t err = esp_ota_get_partition_description(partition, &next_desc);
        if(err != ESP_OK) {
            LOG_MSG_ERROR(FWUP_ERROR_LOG_EN,
                          "esp_ota_get_partition_description failed: %s", esp_err_to_name(err));
            return PAL_ERROR_NOT_FOUND;
        }

        /* Copy into a static so we can point desc at it below */
        static esp_app_desc_t s_next_desc;
        memcpy(&s_next_desc, &next_desc, sizeof(esp_app_desc_t));
        desc = &s_next_desc;
    }

    /* --- Map esp_app_desc_t → pal_bin_info_t --- */
    strncpy(info->version,      desc->version,     sizeof(info->version)      - 1);
    strncpy(info->project_name, desc->project_name,sizeof(info->project_name) - 1);
    strncpy(info->build_date,   desc->date,        sizeof(info->build_date)   - 1);
    strncpy(info->build_time,   desc->time,        sizeof(info->build_time)   - 1);
    strncpy(info->idf_version,  desc->idf_ver,     sizeof(info->idf_version)  - 1);
    memcpy(info->elf_sha256, desc->app_elf_sha256, 32);
    info->secure_version = desc->secure_version;
    info->valid          = true;

    return PAL_OK;
}

void pal_fw_update_print_bin_info(pal_bin_slot_t slot)
{
    pal_bin_info_t info;
    int32_t ret = pal_fw_update_get_bin_info(slot, &info);

    const char* slot_name = (slot == PAL_BIN_SLOT_RUNNING) ? "RUNNING" : "NEXT";

    if(ret != PAL_OK || !info.valid)
    {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN,
                      "[BIN INFO] slot=%-7s — not available (err=%d)", slot_name, (int)ret);
        return;
    }

    /* Format elf_sha256 as a hex string for printing */
    char sha_hex[65] = {};
    for(int i = 0; i < 32; i++) {
        snprintf(sha_hex + i * 2, 3, "%02x", info.elf_sha256[i]);
    }

    LOG_MSG_INFO(LOG_EN, "");
    LOG_MSG_INFO(LOG_EN, "============================================================");
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] slot        : %s",    slot_name);
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] version     : %s",    info.version);
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] project     : %s",    info.project_name);
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] build date  : %s %s", info.build_date, info.build_time);
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] idf version : %s",    info.idf_version);
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] secure ver  : %lu",   (unsigned long)info.secure_version);
    LOG_MSG_INFO(LOG_EN, "[BIN INFO] elf sha256  : %s",    sha_hex);
    LOG_MSG_INFO(LOG_EN, "============================================================");
    LOG_MSG_INFO(LOG_EN, "");
}
