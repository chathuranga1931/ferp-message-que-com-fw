// module_plog.cpp
//
// ModulePLog — persistent SD-card logger.

#include "module_plog.h"
#include "msg_persist_log.h"
#include "msg_sd_ready.h"
#include "app_msg_codec.h"
#include "app_sd.h"
#include "hsys_module.h"
#include "pal_logger.h"
#include "hsys_msg.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>

#define __TAG__       "PLOG    "
#define PLOG_LOG_EN   true

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModulePLog s_instance;
ModulePLog *ModulePLog::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Static publish callback — registered with HsysModule::register_plog_fn().
// Called from any task (via log_persistent()) to post a MsgPersistLog.
// The actual file write happens on storage_task when the message is dispatched.
// ---------------------------------------------------------------------------

static void _plog_publish_fn(hsys_module_id_t sender_id, const char *text)
{
    MsgPersistLog::Payload p = {};
    strncpy(p.text, text, sizeof(p.text) - 1);
    hsys_msg_t *msg = MsgPersistLog::create(sender_id, p);
    if (msg) {
        hsys_msg_publish(msg);
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ModulePLog::set_msg_table(const hsys_msg_id_t *ids, uint8_t count)
{
    _msg_ids   = ids;
    _msg_count = count;
}

void ModulePLog::init()
{
    // Register the global log_persistent() trampoline before subscribing so
    // other modules initialised after this one can call it immediately.
    HsysModule::register_plog_fn(_plog_publish_fn);

    // Always listen for the log message and the SD-ready signal.
    subscribe(MsgPersistLog::ID);
    subscribe(MSG_ID_SD_READY);

    // Auto-subscribe to every caller-supplied message ID.
    for (uint8_t i = 0; i < _msg_count; i++) {
        subscribe(_msg_ids[i]);
    }

    LOG_MSG_INFO(PLOG_LOG_EN, "init — %u auto-log IDs registered", (unsigned)_msg_count);
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------

void ModulePLog::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MSG_ID_SD_READY:
            _on_sd_ready();
            break;
        case MSG_ID_PERSIST_LOG:
            _on_persist_log(msg);
            break;
        default:
            _on_auto_msg(msg);
            break;
    }
}

// ---------------------------------------------------------------------------
// SD-ready handler — create directory, restore write position from state file
// ---------------------------------------------------------------------------

void ModulePLog::_on_sd_ready()
{
    if (!_storage) {
        LOG_MSG_WARNING(PLOG_LOG_EN, "no storage interface — persistent log disabled");
        return;
    }

    // Ensure /sd/plog/ directory exists.
    app_sd_create_dir(MODULE_PLOG_ROOT, 2000);

    // Try to read the resume state.
    char buf[32] = {};
    size_t bytes_read = 0;
    int32_t r = app_sd_read_file(MODULE_PLOG_STATE_FILE, buf, sizeof(buf) - 1, &bytes_read, 500);
    if (r == 0 && bytes_read > 0) {
        unsigned idx   = 0;
        unsigned lcount = 0;
        if (sscanf(buf, "%u %u", &idx, &lcount) == 2
            && idx   <  (unsigned)MODULE_PLOG_MAX_FILES
            && lcount <= (unsigned)MODULE_PLOG_LINES_PER_FILE)
        {
            _file_idx   = (uint8_t)idx;
            _line_count = lcount;
            _sd_ready   = true;
            LOG_MSG_INFO(PLOG_LOG_EN, "resumed at log_%03u.txt line %lu",
                         (unsigned)_file_idx, (unsigned long)_line_count);
            return;
        }
    }

    // No valid state — start fresh at file 000.
    _file_idx   = 0;
    _line_count = 0;
    _sd_ready   = true;
    _save_state();
    LOG_MSG_INFO(PLOG_LOG_EN, "starting fresh at log_000.txt");
}

// ---------------------------------------------------------------------------
// MsgPersistLog handler — write caller-supplied text string
// ---------------------------------------------------------------------------

void ModulePLog::_on_persist_log(const hsys_msg_t &msg)
{
    if (!_sd_ready) return;

    auto p = MsgPersistLog::deserialize(msg);

    // Timestamp
    char ts[24];
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    // Sender module name (best effort)
    const char *sender = "?";
    HsysModule *mod = hsys_module_find(msg.sender_id);
    if (mod) {
        sender = mod->name();
    }

    char line[256];
    snprintf(line, sizeof(line), "[%s] [%s] %s", ts, sender, p.text);
    _write_line(line);
}

// ---------------------------------------------------------------------------
// Auto-message handler — encode to JSON and log as a structured entry
// ---------------------------------------------------------------------------

void ModulePLog::_on_auto_msg(const hsys_msg_t &msg)
{
    if (!_sd_ready) return;

    char msg_name[APP_MSG_CODEC_MSG_NAME_MAX]  = {};
    char data_json[APP_MSG_CODEC_DATA_JSON_MAX] = {};

    int32_t r = app_msg_codec_encode(&msg,
                                     msg_name, sizeof(msg_name),
                                     data_json, sizeof(data_json));
    if (r != 0) {
        return;  // not in codec table — skip silently
    }

    char ts[24];
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);

    char line[640];
    snprintf(line, sizeof(line), "[%s] MSG:%s %s", ts, msg_name, data_json);
    _write_line(line);
}

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

void ModulePLog::_build_path(char *buf, size_t len, uint8_t idx) const
{
    snprintf(buf, len, "%s/log_%03u.txt", MODULE_PLOG_ROOT, (unsigned)idx);
}

bool ModulePLog::_write_line(const char *line)
{
    if (!_sd_ready || !_storage || !_storage->append_line) {
        return false;
    }

    char path[64];
    _build_path(path, sizeof(path), _file_idx);

    int32_t r = _storage->append_line(path, line, 2000);
    if (r != 0) {
        LOG_MSG_WARNING(PLOG_LOG_EN, "append_line failed (%ld)", (long)r);
        return false;
    }

    _line_count++;

    if (_line_count >= (uint32_t)MODULE_PLOG_LINES_PER_FILE) {
        _advance_file();
    }

    return true;
}

void ModulePLog::_advance_file()
{
    _file_idx   = (uint8_t)((_file_idx + 1) % MODULE_PLOG_MAX_FILES);
    _line_count = 0;

    // Delete the stale file at the new slot (from a previous cycle) and
    // create a fresh empty file so append_line has a clean target.
    char path[64];
    _build_path(path, sizeof(path), _file_idx);
    _storage->delete_file(path, 1000);
    _storage->create_file(path, 1000);

    _save_state();

    LOG_MSG_INFO(PLOG_LOG_EN, "rolled to log_%03u.txt", (unsigned)_file_idx);
}

void ModulePLog::_save_state()
{
    if (!_storage || !_storage->write_file) return;
    char buf[24];
    snprintf(buf, sizeof(buf), "%u %lu\n",
             (unsigned)_file_idx, (unsigned long)_line_count);
    _storage->write_file(MODULE_PLOG_STATE_FILE, buf, 1000);
}
