// module_plog.h
//
// ModulePLog — persistent SD-card logger.
//
// Responsibilities:
//   1. Subscribe to MsgPersistLog (published by any module via
//      HsysModule::log_persistent()) and write the text to a rotating log file.
//   2. Subscribe to an optional user-provided table of message IDs.  When one
//      of those messages arrives, encode it to JSON via app_msg_codec_encode()
//      and write the result to the log as a structured entry.
//
// Log file layout:
//   /sd/plog/log_000.txt  …  /sd/plog/log_099.txt   (100 files, circular)
//   /sd/plog/state.txt                                (resume bookmark)
//
//   Each file holds up to MODULE_PLOG_LINES_PER_FILE lines.  When the limit
//   is reached the module rolls to the next slot, deleting the old file.
//   After slot 099 it wraps back to 000 (oldest data is overwritten).
//
// Line format:
//   Text log:  [YYYY-MM-DD HH:MM:SS] [module_name] text\n
//   Msg log:   [YYYY-MM-DD HH:MM:SS] MSG:ClassName {"field":value,...}\n
//
// Wiring (in app.cpp, before app_init):
//   ModulePLog::instance()->set_storage(app_sd_get_storage_interface());
//   ModulePLog::instance()->set_msg_table(k_plog_msg_ids,
//                                         sizeof(k_plog_msg_ids)/sizeof(k_plog_msg_ids[0]));
//
// Any module may log at runtime via the base-class helper:
//   log_persistent("nozzle %u pumped %.3f L", idx, vol);

#pragma once

#include "hsys_module.h"
#include "storage.h"
#include "app_module_ids.h"
#include "app_msg_ids.h"

// ---------------------------------------------------------------------------
// Configuration constants
// ---------------------------------------------------------------------------

#define MODULE_PLOG_NAME            "plog"
#define MODULE_PLOG_MAX_FILES       100    ///< log_000.txt .. log_099.txt
#define MODULE_PLOG_LINES_PER_FILE  400    ///< rotate to next file at this threshold
#define MODULE_PLOG_ROOT            "/sd/plog"
#define MODULE_PLOG_STATE_FILE      "/sd/plog/state.txt"

// ---------------------------------------------------------------------------
// ModulePLog
// ---------------------------------------------------------------------------

class ModulePLog : public HsysModule
{
public:
    ModulePLog() : HsysModule(MODULE_PLOG_ID, MODULE_PLOG_NAME) {}

    static ModulePLog *instance();

    /** Wire the SD storage interface (same pattern as ModuleCloud).
     *  Must be called before app_init(). */
    void set_storage(const storage_interface_t *storage) { _storage = storage; }

    /** Pass the list of message IDs to auto-subscribe and log.
     *  The array must stay valid for the firmware lifetime (typically in flash/rodata).
     *  Pass nullptr / 0 to disable auto-logging of bus messages. */
    void set_msg_table(const hsys_msg_id_t *ids, uint8_t count);

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    const storage_interface_t *_storage    = nullptr;
    bool                       _sd_ready   = false;

    const hsys_msg_id_t *_msg_ids   = nullptr;
    uint8_t              _msg_count = 0;

    uint8_t  _file_idx   = 0;   ///< current log file index  (0 – 99)
    uint32_t _line_count = 0;   ///< lines written in the current file

    // ── Handlers ──────────────────────────────────────────────────────────────
    void _on_sd_ready();
    void _on_persist_log(const hsys_msg_t &msg);
    void _on_auto_msg(const hsys_msg_t &msg);

    // ── File helpers ──────────────────────────────────────────────────────────
    bool _write_line(const char *line);
    void _advance_file();
    void _save_state();
    void _build_path(char *buf, size_t len, uint8_t idx) const;
};
