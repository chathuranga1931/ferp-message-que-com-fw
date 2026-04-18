// hsys_module.h
//
// Module base class and registry API.
//
// Every application module inherits from HsysModule and overrides the
// lifecycle hooks and the message handler.  The framework calls the three
// init phases in order across ALL registered modules before any task starts:
//
//   Phase 1 — pre_init()   : hardware / driver setup, no inter-module calls
//   Phase 2 — init()       : subscribe to messages, set up internal state
//   Phase 3 — post_init()  : cross-module wiring that depends on phase 2
//
// At runtime the dispatch loop calls on_msg_received() for every message
// routed to this module.
//
// Usage (in a subclass):
//
//   class ModuleA : public HsysModule {
//   public:
//       ModuleA() : HsysModule(MODULE_A_ID, "module_a") {}
//   protected:
//       void init()         override { subscribe(MSG_TICK_1000MS); }
//       void on_msg_received(const hsys_msg_t &msg) override { ... }
//   };

#ifndef HSYS_MODULE_H
#define HSYS_MODULE_H

#include "hsys_types.h"
#include "hsys_config.h"
#include "hsys_msg.h"

// ---------------------------------------------------------------------------
// HsysModule — abstract base class
// ---------------------------------------------------------------------------

class HsysModule
{
public:
    // -----------------------------------------------------------------------
    // Construction / identity
    // -----------------------------------------------------------------------

    /**
     * @param module_id  Unique non-zero ID for this module.
     * @param name       Human-readable name used in log output.
     */
    HsysModule(hsys_module_id_t module_id, const char *name);

    virtual ~HsysModule() = default;

    /** Returns this module's unique ID. */
    hsys_module_id_t id()   const { return m_id;   }

    /** Returns this module's name string. */
    const char      *name() const { return m_name; }

    // -----------------------------------------------------------------------
    // Lifecycle hooks  (override in subclass as needed)
    // -----------------------------------------------------------------------

    /**
     * Phase 1 — called for ALL modules before init() runs on any module.
     * Use for low-level hardware / peripheral initialisation.
     * Do NOT call subscribe() or publish() here.
     */
    virtual void pre_init()  {}

    /**
     * Phase 2 — the main init hook.
     * Call subscribe() here to register message interests.
     * Do NOT publish messages or call other modules.
     */
    virtual void init()      {}

    /**
     * Phase 3 — called for ALL modules after init() has run on every module.
     * Safe to publish a one-shot startup message or query another module's state.
     */
    virtual void post_init() {}

    /**
     * Runtime message handler — called by the dispatch loop.
     * Must be non-blocking.  Do NOT store the reference beyond this call.
     *
     * @param msg  The received message.
     */
    virtual void on_msg_received(const hsys_msg_t &msg) = 0;

    // -----------------------------------------------------------------------
    // Internal dispatch bridge (called by the framework, not by subclasses)
    // -----------------------------------------------------------------------
    void dispatch(const hsys_msg_t &msg) { on_msg_received(msg); }

protected:
    // -----------------------------------------------------------------------
    // Helpers available to subclasses
    // -----------------------------------------------------------------------

    /**
     * Subscribe this module to a message ID.
     * Typically called from init().
     */
    hsys_status_t subscribe(hsys_msg_id_t msg_id);

    /**
     * Create a raw message instance via the framework factory.
     * Prefer create_typed<T>() for typed message classes.
     * Returns nullptr if msg_id is unknown or the pool is full.
     */
    hsys_msg_t *create_msg(hsys_msg_id_t msg_id);

    /**
     * Typed factory helper — creates, serializes, and returns a framework
     * message in one call.  The module's own ID is injected as sender_id
     * automatically so callers never pass it manually.
     *
     * Usage:
     *   auto *msg = create_typed<MsgSensorData>(payload);
     *   publish(msg);
     *
     * MsgClass must expose:
     *   static hsys_msg_t *create(hsys_module_id_t sender, const Payload &p);
     */
    template<typename MsgClass>
    hsys_msg_t *create_typed(const typename MsgClass::Payload &payload)
    {
        return MsgClass::create(m_id, payload);
    }

    /**
     * Publish a previously created message onto the bus.
     * The framework stamps ref_count and enqueues the pointer to every
     * subscriber's queue.  Do NOT access the message after this call.
     */
    hsys_status_t publish(hsys_msg_t *msg);

    /**
     * Send a point-to-point (DIRECT) message to a specific module.
     * Sets msg->receiver_id and calls hsys_msg_send().
     * Do NOT access the message after this call.
     *
     * @param msg          Message created with create_msg() / create_typed().
     * @param receiver_id  Destination module ID.
     * @param timeout_ms   Queue send timeout (0 = non-blocking).
     */
    hsys_status_t send(hsys_msg_t      *msg,
                       hsys_module_id_t receiver_id,
                       uint32_t         timeout_ms = 0);

    /**
     * Log an informational message via the active hsys_log hook.
     * Identical signature to printf — routes through hsys_log() which
     * the product layer can override to redirect to pal_logger.
     */
    void log(const char *fmt, ...) const;

    /**
     * Log an error message via the active hsys_log_error hook.
     */
    void log_error(const char *fmt, ...) const;

private:
    hsys_module_id_t  m_id;
    const char       *m_name;
};

// ---------------------------------------------------------------------------
// Module registry  (C linkage so main.c / task_mgr.cpp can call it)
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the module registry and register all modules in one call.
 *
 *         Replaces separate hsys_module_init() + hsys_module_register() calls.
 *         The array must remain valid for the lifetime of the firmware.
 *
 * @param  modules      Array of HsysModule pointers to register.
 * @param  count        Number of entries in the array.
 * @return HSYS_OK on success.
 */
hsys_status_t hsys_module_init(HsysModule **modules, uint8_t count);

/**
 * @brief  Look up a registered module by ID.
 * @return Pointer to the module, or nullptr if not found.
 */
HsysModule *hsys_module_find(hsys_module_id_t module_id);

/**
 * @brief  Dispatch a message to the module identified by msg->receiver_id.
 *         Called internally by the task dispatch loop.
 */
hsys_status_t hsys_module_dispatch(const hsys_msg_t *msg);

/**
 * @brief  Dispatch a message to every bound module that subscribed to
 *         msg->msg_id.  This must be used instead of hsys_module_dispatch()
 *         in the task loop because receiver_id in the shared header may have
 *         been overwritten by the time the task dequeues the pointer
 *         (publish sets receiver_id just before each enqueue — it is not safe
 *         to read after the fact).
 */
void hsys_module_dispatch_for_task(const hsys_msg_t        *msg,
                                   const hsys_module_id_t  *module_ids,
                                   uint8_t                  count);

/**
 * @brief  Run Phase 1 (pre_init) only on modules bound to the given task.
 *         Called internally by each task's dispatch loop startup sequence.
 */
void hsys_module_pre_init_for_task(const hsys_module_id_t *module_ids, uint8_t count);

/**
 * @brief  Run Phase 2 (init) only on modules bound to the given task.
 */
void hsys_module_init_for_task(const hsys_module_id_t *module_ids, uint8_t count);

/**
 * @brief  Run Phase 3 (post_init) only on modules bound to the given task.
 */
void hsys_module_post_init_for_task(const hsys_module_id_t *module_ids, uint8_t count);

#ifdef __cplusplus
}
#endif

#endif // HSYS_MODULE_H
