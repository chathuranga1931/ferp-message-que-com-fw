// module_sysmon.cpp
//
// System Monitor — polls hsys_pool and message-header stats, prints report.

#include "module_sysmon.h"
#include "hsys_pool.h"
#include "hsys_msg.h"
#include "msg_tick_1000ms.h"
#include "msg_pool_get_json.h"
#include "msg_pool_json.h"
#include "msg_get_file_list_spiffs.h"
#include "msg_file_list_spiffs.h"
#include "msg_system_reboot.h"
#include "app_spiffs.h"
#include "pal_power.h"
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static ModuleSysmon s_instance;

ModuleSysmon *ModuleSysmon::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Lifecycle — Phase 2
// ---------------------------------------------------------------------------

void ModuleSysmon::init()
{
    log("init");
    subscribe(MsgTick1000ms::ID);
    subscribe(MsgPoolGetJson::ID);
    subscribe(MsgGetFileListSpiffs::ID);
    subscribe(MsgSystemReboot::ID);
}

// ---------------------------------------------------------------------------
// Runtime message handler
// ---------------------------------------------------------------------------

void ModuleSysmon::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MsgTick1000ms::ID:
            ++m_tick_count;
            if (m_tick_count % SYSMON_REPORT_INTERVAL_TICKS == 0) {
                print_report();
            }
            break;
        case MsgPoolGetJson::ID:
            _publish_pool_json();
            break;
        case MsgGetFileListSpiffs::ID:
            _publish_file_list_spiffs();
            break;
        case MsgSystemReboot::ID:
            log("reboot requested — calling pal_power_reset()");
            pal_power_reset();
            break; // never reached
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Report printer
// ---------------------------------------------------------------------------

void ModuleSysmon::print_report()
{

}

void ModuleSysmon::_publish_pool_json(){
    JsonDocument doc;
    JsonArray classes = doc["classes"].to<JsonArray>();

    uint8_t idx = 0;
    hsys_pool_class_info_t info;
    while (hsys_pool_get_info(idx, &info) == HSYS_OK) {
        JsonObject entry = classes.add<JsonObject>();
        entry["bs"]   = info.block_size;
        entry["tot"]  = info.total_count;
        entry["free"] = info.free_count;
        entry["peak"] = info.peak_used;
        ++idx;
    }

    char local_buf[512];
    size_t written = serializeJson(doc, local_buf, sizeof(local_buf));
    if (written == 0) {
        log("_publish_pool_json: serializeJson produced empty output");
        return;
    }

    hsys_msg_t *msg = MsgPoolJson::create(id(), local_buf, (uint32_t)written);
    if (!msg) {
        log("_publish_pool_json: MsgPoolJson::create failed");
        return;
    }
    publish(msg);
}

// ---------------------------------------------------------------------------
// _publish_file_list_spiffs()
// ---------------------------------------------------------------------------

void ModuleSysmon::_publish_file_list_spiffs()
{
    // Gather SPIFFS partition info.
    app_spiffs_info_t spiffs_info = {};
    app_spiffs_get_info(&spiffs_info);

    // Build the JSON into a 2 KiB pool buffer (fits the 2048-byte pool class).
    // Keys are shortened to minimise payload: f=files, n=name, s=size.
    static const uint16_t BUF_SIZE = 2048;
    char *json_buf = static_cast<char *>(hsys_pool_alloc(BUF_SIZE));
    if (!json_buf) {
        log("_publish_file_list_spiffs: pool alloc failed");
        return;
    }

    JsonDocument doc;
    JsonArray files = doc["f"].to<JsonArray>();

    // Enumerate files via app_spiffs (mutex-protected, resource-safe).
    struct ListCtx { JsonArray *arr; };
    ListCtx list_ctx = { &files };
    app_spiffs_list_files([](const char *name, size_t size, void *ctx) {
        JsonArray *arr = static_cast<ListCtx *>(ctx)->arr;
        JsonObject f = arr->add<JsonObject>();
        f["n"] = name;
        f["s"] = size;
    }, &list_ctx, 2000);

    size_t written = serializeJson(doc, json_buf, BUF_SIZE);
    if (written == 0) {
        log("_publish_file_list_spiffs: serializeJson produced empty output");
        hsys_pool_free(json_buf);
        return;
    }

    // MsgFileListSpiffs::create() takes ownership of the JSON by copying it
    // into its own pool buffer — so we free our temporary buffer first.
    hsys_msg_t *resp = MsgFileListSpiffs::create(id(), json_buf, (uint32_t)written);
    hsys_pool_free(json_buf);

    if (!resp) {
        log("_publish_file_list_spiffs: MsgFileListSpiffs::create failed");
        return;
    }
    publish(resp);
}

#if 0
void ModuleSysmon::print_report()
{
    log("\xe2\x94\x80\xe2\x94\x80 Pool status  (t=%lu s) \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
        (unsigned long)m_tick_count);
    log("  %-6s  %-10s  %-6s  %-6s  %-6s  %s",
        "class", "block_size", "total", "free", "used", "util%");
    log("  %-6s  %-10s  %-6s  %-6s  %-6s  %s",
        "-----", "----------", "-----", "----", "----", "-----");

    uint8_t idx = 0;
    hsys_pool_class_info_t info;

    while (hsys_pool_get_info(idx, &info) == HSYS_OK) {
        uint16_t used = info.total_count - info.free_count;
        uint32_t util = (info.total_count > 0)
                        ? (uint32_t)used * 100u / info.total_count
                        : 0u;

        log("  %-6u  %-10u  %-6u  %-6u  %-6u  %lu%%",
            idx,
            (unsigned)info.block_size,
            (unsigned)info.total_count,
            (unsigned)info.free_count,
            (unsigned)used,
            (unsigned long)util);
        ++idx;
    }

    // Message-header pool stats
    hsys_msg_header_pool_info_t hdr;
    hsys_msg_get_header_pool_info(&hdr);

    log("\xe2\x94\x80\xe2\x94\x80 Msg-header pool \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80");
    log("  slots=%-4u  free=%-4u  used=%-4u  peak_used=%-4u",
        (unsigned)hdr.total_slots,
        (unsigned)hdr.free_slots,
        (unsigned)hdr.used_slots,
        (unsigned)hdr.peak_used_slots);
}
#endif
