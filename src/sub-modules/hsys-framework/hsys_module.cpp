// hsys_module.cpp
//
// Module base class implementation and registry.
//
// hsys_module_init() accepts the full module table from main.c and
// registers all modules in one shot.
//
// The three lifecycle phases are NOT called from main.c.  Instead, each
// task's dispatch loop calls the per-task phase runners for its own bound
// modules.  The task manager coordinates a global barrier so that all tasks
// complete phase N before any task starts phase N+1.

#include "hsys_module.h"
#include "hsys_mutex.h"
#include "hsys_log.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal registry state
// ---------------------------------------------------------------------------

static HsysModule         *s_registry[HSYS_MAX_MODULES];
static uint16_t            s_count       = 0;
static hsys_mutex_handle_t s_mutex       = nullptr;
static bool                s_initialised = false;

// ---------------------------------------------------------------------------
// HsysModule — base class implementation
// ---------------------------------------------------------------------------

HsysModule::HsysModule(hsys_module_id_t module_id, const char *name)
    : m_id(module_id), m_name(name)
{}

hsys_status_t HsysModule::subscribe(hsys_msg_id_t msg_id)
{
    return hsys_msg_subscribe(msg_id, m_id);
}

hsys_msg_t *HsysModule::create_msg(hsys_msg_id_t msg_id)
{
    return hsys_msg_create(msg_id, m_id);
}

hsys_status_t HsysModule::publish(hsys_msg_t *msg)
{
    if (!msg) return HSYS_ERR_NULL;
    return hsys_msg_publish(msg);
}

void HsysModule::log(const char *fmt, ...) const
{
    va_list ap;
    va_start(ap, fmt);
    // Build "[name] <user message>\n" and forward through the hook
    char buf[256];
    int off = snprintf(buf, sizeof(buf), "[%s] ", m_name);
    if (off > 0 && (size_t)off < sizeof(buf))
        vsnprintf(buf + off, sizeof(buf) - (size_t)off, fmt, ap);
    va_end(ap);
    hsys_log("%s\n", buf);
}

void HsysModule::log_error(const char *fmt, ...) const
{
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    int off = snprintf(buf, sizeof(buf), "[%s] ERROR: ", m_name);
    if (off > 0 && (size_t)off < sizeof(buf))
        vsnprintf(buf + off, sizeof(buf) - (size_t)off, fmt, ap);
    va_end(ap);
    hsys_log_error("%s\n", buf);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

hsys_status_t hsys_module_init(HsysModule **modules, uint8_t count)
{
    if (s_initialised) return HSYS_OK;
    if (modules == nullptr || count == 0) return HSYS_ERR_INVALID;

    memset(s_registry, 0, sizeof(s_registry));
    s_count = 0;

    s_mutex = hsys_mutex_create();
    if (s_mutex == nullptr) return HSYS_ERR_NO_MEM;

    s_initialised = true;

    // Register every module from the supplied table
    for (uint8_t i = 0; i < count; i++) {
        if (modules[i] == nullptr) continue;
        if (modules[i]->id() == HSYS_MODULE_ID_INVALID) continue;
        if (s_count >= HSYS_MAX_MODULES) return HSYS_ERR_NO_MEM;
        s_registry[s_count++] = modules[i];
    }

    return HSYS_OK;
}

HsysModule *hsys_module_find(hsys_module_id_t module_id)
{
    if (!s_initialised) return nullptr;
    for (uint16_t i = 0; i < s_count; i++) {
        if (s_registry[i]->id() == module_id) return s_registry[i];
    }
    return nullptr;
}

hsys_status_t hsys_module_dispatch(const hsys_msg_t *msg)
{
    if (msg == nullptr) return HSYS_ERR_NULL;
    HsysModule *m = hsys_module_find(msg->receiver_id);
    if (m == nullptr) return HSYS_ERR_NOT_FOUND;
    m->dispatch(*msg);
    return HSYS_OK;
}

void hsys_module_dispatch_for_task(const hsys_msg_t       *msg,
                                   const hsys_module_id_t *module_ids,
                                   uint8_t                 count)
{
    if (msg == nullptr) return;
    for (uint8_t i = 0; i < count; i++) {
        if (!hsys_msg_is_subscriber(msg->msg_id, module_ids[i])) continue;
        HsysModule *m = hsys_module_find(module_ids[i]);
        if (m) m->dispatch(*msg);
    }
}

// ---------------------------------------------------------------------------
// Per-task lifecycle phase runners
// Called by the task manager's dispatch_loop startup sequence.
// Only the modules bound to *this* task are initialised here.
// ---------------------------------------------------------------------------

void hsys_module_pre_init_for_task(const hsys_module_id_t *ids, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        HsysModule *m = hsys_module_find(ids[i]);
        if (m) {
            hsys_log("[hsys] pre_init  \xe2\x86\x92 %s\n", m->name());
            m->pre_init();
        }
    }
}

void hsys_module_init_for_task(const hsys_module_id_t *ids, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        HsysModule *m = hsys_module_find(ids[i]);
        if (m) {
            hsys_log("[hsys] init      \xe2\x86\x92 %s\n", m->name());
            m->init();
        }
    }
}

void hsys_module_post_init_for_task(const hsys_module_id_t *ids, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        HsysModule *m = hsys_module_find(ids[i]);
        if (m) {
            hsys_log("[hsys] post_init \xe2\x86\x92 %s\n", m->name());
            m->post_init();
        }
    }
}

