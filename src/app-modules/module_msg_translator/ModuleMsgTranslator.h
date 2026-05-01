// ModuleMsgTranslator.h
//
// ModuleMsgTranslator — application-layer message translation and routing.
//
// Purpose:
//   Owns a compile-time translation table (defined in app.cpp).  For every
//   incoming message that matches a table row the module calls the associated
//   translator function and publishes the returned message onto the bus.
//   Multiple rows with the same incoming message ID are all executed in order.
//
// Table layout (msg_translator_entry_t):
//   ┌─────────────┬──────────┬──────────────┬──────────┬────────────┬─────────┬──────────┐
//   │ in_msg_id   │ in_src   │ out_msg_id   │ out_dest │ translator │ delayed │ delay_ms │
//   └─────────────┴──────────┴──────────────┴──────────┴────────────┴─────────┴──────────┘
//   in_src  = 0  → accept from any sender
//   out_dest = 0 → broadcast (publish), otherwise send() to that module
//
// Table ownership:
//   The table must have static lifetime.  Call set_table() once before
//   app_init() starts (e.g. in app_init() before hsys_module_init()).
//
// Translator function prototype (msg_translator_fn_t):
//   hsys_msg_t *fn(in_src, in_msg, out_msg_id, out_dest)
//   Return a newly-allocated hsys_msg_t* to publish, or nullptr to suppress.
//
// Threading:
//   Runs on its own dedicated RTOS task ("xlat_task").
//   on_msg_received() is non-blocking; translator functions must be too.
//
// Delayed publish:
//   When delayed == true the entry is noted and delay_ms is stored, but
//   publishing is currently performed immediately (software-timer integration
//   is a future enhancement).  A warning is logged on the first such dispatch.

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include <stdint.h>

#define MODULE_MSG_TRANSLATOR_NAME  "msg_xlat"

// ---------------------------------------------------------------------------
// Translator function prototype
//
// Parameters:
//   in_src     — sender module ID extracted from the incoming message header
//   in_msg     — the incoming message (read-only; pointer is invalid after return)
//   out_msg_id — the target message ID specified in the table row
//   out_dest   — the target destination module ID from the table row (0 = broadcast)
//
// Returns:
//   A new hsys_msg_t* allocated from the pool and ready to publish, or nullptr
//   to suppress publishing for this entry.
//   Ownership transfers to ModuleMsgTranslator on non-null return.
// ---------------------------------------------------------------------------
typedef hsys_msg_t *(*msg_translator_fn_t)(
    hsys_module_id_t   in_src,
    const hsys_msg_t  *in_msg,
    hsys_msg_id_t      out_msg_id,
    hsys_module_id_t   out_dest
);

// ---------------------------------------------------------------------------
// One row in the translation table
// ---------------------------------------------------------------------------
typedef struct {
    hsys_msg_id_t       in_msg_id;   ///< Incoming message ID to watch
    hsys_module_id_t    in_src;      ///< Required sender module ID; 0 = match any sender
    hsys_msg_id_t       out_msg_id;  ///< Outgoing message ID the translator should produce
    hsys_module_id_t    out_dest;    ///< Destination module; 0 = broadcast (publish)
    msg_translator_fn_t translator;  ///< Translation function (must not be nullptr)
    bool                delayed;     ///< Reserved — true = delayed publish (not yet active)
    uint32_t            delay_ms;    ///< Delay in ms (used when delayed == true)
} msg_translator_entry_t;

// ---------------------------------------------------------------------------
// ModuleMsgTranslator
// ---------------------------------------------------------------------------
class ModuleMsgTranslator : public HsysModule
{
public:
    ModuleMsgTranslator() : HsysModule(MODULE_MSG_TRANSLATOR_ID, MODULE_MSG_TRANSLATOR_NAME) {}

    static ModuleMsgTranslator *instance();

    /**
     * Load the application's translation table.
     * Must be called before app_init() triggers init().
     * Both pointers must remain valid for the lifetime of the module.
     *
     * @param table  Pointer to the first entry in the table.
     * @param count  Number of entries.
     */
    void set_table(const msg_translator_entry_t *table, uint16_t count);

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    const msg_translator_entry_t *_table = nullptr;
    uint16_t                      _count = 0;

    void _dispatch_entry(const msg_translator_entry_t &entry, const hsys_msg_t &in_msg);
};
