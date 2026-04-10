// IHsysMsg.h
//
// Abstract interface for typed application messages.
//
// Every concrete message class (e.g. MsgSensorData, MsgTick1000ms) must:
//
//   1. Inherit publicly from IHsysMsg.
//   2. Declare a static constexpr hsys_msg_id_t ID.
//   3. Declare a static constexpr hsys_msg_desc_t DESCRIPTOR.
//   4. Declare an inner `Payload` struct (may be empty for zero-payload msgs).
//   5. Implement the two virtual methods: msg_id() and serialize().
//   6. Provide static create(sender_id, payload) and deserialize(msg) helpers.
//
// The framework layer (hsys_msg.cpp, hsys_task_mgr.cpp, hsys_pool.cpp) is
// completely unaware of this interface — it only ever sees hsys_msg_t*.
//
// Usage (publisher):
//
//   MsgSensorData::Payload p{ .counter = 1, .temperature = 22.5f };
//   hsys_msg_t *msg = create_typed<MsgSensorData>(p);   // from HsysModule
//   publish(msg);
//
// Usage (subscriber):
//
//   case MsgSensorData::ID: {
//       auto p = MsgSensorData::deserialize(msg);
//       // use p.counter, p.temperature
//   }

#ifndef IHSYS_MSG_H
#define IHSYS_MSG_H

#include "hsys_msg.h"   // hsys_msg_t, hsys_msg_desc_t

// ---------------------------------------------------------------------------
// IHsysMsg — abstract base
// ---------------------------------------------------------------------------

class IHsysMsg
{
public:
    virtual ~IHsysMsg() = default;

    /**
     * Returns the message ID that this instance represents.
     * Concrete classes also expose a static constexpr ID for use in
     * switch-case statements without needing an instance.
     */
    virtual hsys_msg_id_t msg_id() const = 0;

    /**
     * Serialize this object's payload into the pre-allocated buffer inside
     * the given hsys_msg_t.  msg->payload must already point to a buffer of
     * at least sizeof(Payload) bytes (guaranteed by hsys_msg_create).
     * Called internally by the static create() factory in each subclass.
     */
    virtual void serialize(hsys_msg_t *msg) const = 0;
};

#endif // IHSYS_MSG_H
