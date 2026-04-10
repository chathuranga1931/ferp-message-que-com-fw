// hsys_msg.h
//
// Message descriptor table, message creation, and message bus API.
//
// Design overview
// ───────────────
// 1. Every message type is described once in a static hsys_msg_desc_t table
//    (defined in app_msg_table.h, registered at startup via hsys_msg_table_init).
//    The descriptor holds the payload size, message type, and permissions —
//    the framework uses it to allocate the right buffer and enforce rules.
//
// 2. Publishers call hsys_msg_create(msg_id) to obtain a ready-to-fill
//    hsys_msg_t*.  The framework allocates the payload buffer internally.
//    The module writes its data into msg->payload, then calls hsys_msg_publish.
//
// 3. The bus stamps a ref_count equal to the number of subscribers at publish
//    time.  Each subscriber task queue receives the same pointer (zero-copy).
//    After each on_msg_received() call returns the dispatch loop calls
//    hsys_msg_release(), which decrements ref_count.  When it reaches zero
//    the payload buffer is returned to the pool automatically.
//
// 4. Modules never call hsys_pool_alloc / hsys_pool_free directly.

#ifndef HSYS_MSG_H
#define HSYS_MSG_H

#include "hsys_types.h"
#include "hsys_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Message type
// ---------------------------------------------------------------------------

typedef enum {
    HSYS_MSG_NOTIFICATION = 0,  ///< Fan-out: delivered to all subscribers
    HSYS_MSG_DIRECT       = 1,  ///< Point-to-point: receiver_id must be set
} hsys_msg_type_t;

// ---------------------------------------------------------------------------
// Permissions  (publish / subscribe access control)
// ---------------------------------------------------------------------------

/** Any module may publish or subscribe — no restriction. */
#define HSYS_PERM_ANY   ((uint32_t)0xFFFFFFFFUL)

/** Nobody may publish or subscribe — message is disabled / reserved. */
#define HSYS_PERM_NONE  ((uint32_t)0x00000000UL)

// ---------------------------------------------------------------------------
// Message descriptor  (one per message type, lives in flash / rodata)
// ---------------------------------------------------------------------------

typedef struct {
    hsys_msg_id_t    msg_id;         ///< Message identifier
    hsys_msg_type_t  msg_type;       ///< NOTIFICATION or DIRECT
    uint16_t         payload_size;   ///< Bytes to allocate for payload (0 = none)
    uint32_t         pub_perm;       ///< Bitmask of module IDs that may publish
    uint32_t         sub_perm;       ///< Bitmask of module IDs that may subscribe
} hsys_msg_desc_t;

/** Convenience macro to fill an hsys_msg_desc_t initialiser. */
#define HSYS_MSG_DESC(id, type, size, pub, sub) \
    { (id), (type), (size), (pub), (sub) }

// ---------------------------------------------------------------------------
// Message instance  (one per in-flight message, ref-counted)
// ---------------------------------------------------------------------------

typedef struct hsys_msg {
    hsys_msg_id_t            msg_id;        ///< Topic identifier
    hsys_module_id_t         sender_id;     ///< Module that created this message
    hsys_module_id_t         receiver_id;   ///< Set for DIRECT; 0 for NOTIFICATION
    hsys_priority_t          priority;      ///< Delivery priority
    hsys_tick_t              timestamp;     ///< Uptime ms stamped at publish time
    void                    *payload;       ///< Pool-allocated buffer (may be NULL)
    uint16_t                 payload_size;  ///< Size of payload in bytes
    volatile uint8_t         ref_count;     ///< Decremented by bus after each delivery
    const hsys_msg_desc_t   *desc;          ///< Pointer back to the static descriptor
} hsys_msg_t;

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise the message bus.
 *         Call once at startup before any modules are registered.
 */
hsys_status_t hsys_msg_init(void);

/**
 * @brief  Register the application message descriptor table.
 *         Must be called after hsys_msg_init() and before any publish/subscribe.
 *
 * @param  table  Pointer to a const array of hsys_msg_desc_t entries.
 * @param  count  Number of entries in the table.
 */
hsys_status_t hsys_msg_table_init(const hsys_msg_desc_t *table, uint16_t count);

// ---------------------------------------------------------------------------
// Subscribe / Unsubscribe
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_subscribe(hsys_msg_id_t msg_id, hsys_module_id_t module_id);
hsys_status_t hsys_msg_unsubscribe(hsys_msg_id_t msg_id, hsys_module_id_t module_id);

// ---------------------------------------------------------------------------
// Message lifecycle  (create → fill payload → publish → auto-release)
// ---------------------------------------------------------------------------

/**
 * @brief  Allocate and initialise a new message instance.
 *         The framework looks up the descriptor for msg_id, allocates the
 *         payload buffer from the pool (if payload_size > 0), and returns a
 *         pointer to the message ready to be filled.
 *
 * @param  msg_id      The message to create.
 * @param  sender_id   Module ID of the publisher.
 * @return Pointer to the ready-to-fill message, or NULL on error.
 */
hsys_msg_t *hsys_msg_create(hsys_msg_id_t msg_id, hsys_module_id_t sender_id);

/**
 * @brief  Publish a message created with hsys_msg_create().
 *         The bus stamps ref_count = subscriber count and enqueues the *pointer*
 *         to each subscriber's task queue.  The caller must not touch the
 *         message after this call.
 *
 * @param  msg  Message returned by hsys_msg_create(), payload already filled.
 * @return HSYS_OK, or an error code if any queue was full.
 */
hsys_status_t hsys_msg_publish(hsys_msg_t *msg);

/**
 * @brief  Release one reference to a message.
 *         Called by the dispatch loop after each on_msg_received() returns.
 *         When ref_count reaches zero the payload buffer is returned to the pool
 *         and the message header is freed automatically.
 *         Modules must NOT call this directly.
 *
 * @param  msg  Message to release.
 */
void hsys_msg_release(hsys_msg_t *msg);

// ---------------------------------------------------------------------------
// Direct send  (bypasses subscription table — receiver_id must be set)
// ---------------------------------------------------------------------------

/**
 * @brief  Send a message directly to one specific module.
 *         msg must have been created with hsys_msg_create() and have
 *         receiver_id set before calling this function.
 *
 * @param  msg         Message to send (receiver_id pre-filled).
 * @param  timeout_ms  Max wait if the queue is full (0 = non-blocking).
 */
hsys_status_t hsys_msg_send(hsys_msg_t *msg, uint32_t timeout_ms);

// ---------------------------------------------------------------------------
// ISR-safe publish
// ---------------------------------------------------------------------------

hsys_status_t hsys_msg_publish_from_isr(hsys_msg_t *msg,
                                         bool *higher_priority_woken);

// ---------------------------------------------------------------------------
// Subscription query
// ---------------------------------------------------------------------------

/**
 * @brief  Returns true if @p module_id is currently subscribed to @p msg_id.
 *         Used by the dispatch loop to route messages without relying on the
 *         shared (mutable) receiver_id field.
 */
bool hsys_msg_is_subscriber(hsys_msg_id_t msg_id, hsys_module_id_t module_id);

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

/**
 * @brief  Snapshot of the internal message-header pool.
 *         Useful for system monitoring / logging.
 */
typedef struct {
    uint8_t total_slots;     ///< Fixed pool capacity (HSYS_MSG_HEADER_POOL_SIZE)
    uint8_t free_slots;      ///< Currently available slots
    uint8_t used_slots;      ///< Slots currently holding in-flight messages
    uint8_t peak_used_slots; ///< Highest watermark since boot
} hsys_msg_header_pool_info_t;

/**
 * @brief  Fill an hsys_msg_header_pool_info_t with a consistent snapshot.
 *         Safe to call from any task context.
 *
 * @param  info  Output — must not be NULL.
 */
void hsys_msg_get_header_pool_info(hsys_msg_header_pool_info_t *info);

#ifdef __cplusplus
}
#endif

#endif // HSYS_MSG_H
