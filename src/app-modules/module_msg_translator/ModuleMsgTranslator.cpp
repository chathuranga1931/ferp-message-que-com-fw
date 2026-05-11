// ModuleMsgTranslator.cpp
//
// ModuleMsgTranslator implementation — see ModuleMsgTranslator.h for design notes.

#include "ModuleMsgTranslator.h"
#include "pal_logger.h"
#include <stddef.h>

#define __TAG__   "MSG_XLAT"
#define XLAT_LOG  true

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleMsgTranslator s_instance;
ModuleMsgTranslator *ModuleMsgTranslator::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// Table injection — must be called before init() runs
// ---------------------------------------------------------------------------

void ModuleMsgTranslator::set_table(const msg_translator_entry_t *table, uint16_t count)
{
    _table = table;
    _count = count;
}

// ---------------------------------------------------------------------------
// init — subscribe to every unique incoming message ID in the table
// ---------------------------------------------------------------------------

void ModuleMsgTranslator::init()
{
    if (!_table || _count == 0) {
        LOG_MSG_WARNING(XLAT_LOG, "no translation table — module idle");
        return;
    }

    for (uint16_t i = 0; i < _count; i++) {
        hsys_msg_id_t id = _table[i].in_msg_id;

        // Skip duplicate subscriptions for the same message ID.
        bool already_subscribed = false;
        for (uint16_t j = 0; j < i; j++) {
            if (_table[j].in_msg_id == id) {
                already_subscribed = true;
                break;
            }
        }

        if (!already_subscribed) {
            subscribe(id);
            LOG_MSG_INFO(XLAT_LOG, "subscribed → msg 0x%04X", (unsigned)id);
        }
    }

    LOG_MSG_INFO(XLAT_LOG, "init — %u entr%s ready",
                 (unsigned)_count, _count == 1 ? "y" : "ies");
}

// ---------------------------------------------------------------------------
// on_msg_received — find all matching entries and dispatch each one
// ---------------------------------------------------------------------------

void ModuleMsgTranslator::on_msg_received(const hsys_msg_t &msg)
{
    if (!_table) return;

    for (uint16_t i = 0; i < _count; i++) {
        const msg_translator_entry_t &e = _table[i];

        // Match on incoming message ID.
        if (e.in_msg_id != msg.msg_id) continue;

        // Match on sender (0 = accept any sender).
        if (e.in_src != 0 && e.in_src != msg.sender_id) continue;

        // Guard against a nullptr translator.
        if (!e.translator) {
            LOG_MSG_WARNING(XLAT_LOG, "entry[%u]: nullptr translator, skipping", (unsigned)i);
            continue;
        }

        _dispatch_entry(e, msg);
    }
}

// ---------------------------------------------------------------------------
// _dispatch_entry — call translator, then publish or send the result
// ---------------------------------------------------------------------------

void ModuleMsgTranslator::_dispatch_entry(const msg_translator_entry_t &entry,
                                          const hsys_msg_t              &in_msg)
{
    if (entry.delayed && entry.delay_ms > 0) {
        LOG_MSG_WARNING(XLAT_LOG,
                     "entry in_msg=0x%04X: delayed publish not yet implemented — publishing immediately",
                     (unsigned)entry.in_msg_id);
    }

    hsys_msg_t *out = entry.translator(
        in_msg.sender_id,
        &in_msg,
        entry.out_msg_id,
        entry.out_dest
    );

    if (!out) return;  // translator chose to suppress

    if (entry.out_dest != 0) {
        // Point-to-point delivery.
        LOG_MSG_INFO(XLAT_LOG, "0x%04X → send(0x%04X) to module %u",
                     (unsigned)entry.in_msg_id,
                     (unsigned)entry.out_msg_id,
                     (unsigned)entry.out_dest);
        send(out, entry.out_dest);
    } else {
        // Broadcast to all subscribers.
        LOG_MSG_INFO(XLAT_LOG, "0x%04X → publish(0x%04X)",
                     (unsigned)entry.in_msg_id,
                     (unsigned)entry.out_msg_id);
        publish(out);
    }
}
