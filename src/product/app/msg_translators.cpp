// msg_translators.cpp
//
// Application-specific message translator function implementations.
//
// Each function is referenced in the k_translator_table[] defined in app.cpp.
// Declare every function in msg_translators.h before adding it to the table.
//
// Translator contract:
//   - Receive the incoming message and produce a new outgoing message.
//   - Use the concrete message classes (MsgXxx::create / MsgXxx::deserialize)
//     to read the incoming payload and build the outgoing payload.
//   - Return a newly-allocated hsys_msg_t* (from hsys_msg_create / MsgXxx::create),
//     or nullptr to suppress publishing for this entry.
//   - Must be non-blocking; do not sleep or wait for external events.
//
// Example skeleton (uncomment and adapt):
// ---------------------------------------------------------------------------
//
// #include "msg_fuel_pumped.h"
// #include "msg_cloud_status.h"
//
// hsys_msg_t *xlat_fuel_pumped_to_cloud_status(hsys_module_id_t in_src,
//                                              const hsys_msg_t *in_msg,
//                                              hsys_msg_id_t     out_msg_id,
//                                              hsys_module_id_t  out_dest)
// {
//     auto in  = MsgFuelPumped::deserialize(*in_msg);
//     MsgCloudStatus::Payload out{};
//     out.connected  = (in.volume_ml > 0);
//     // ... fill remaining fields ...
//     return MsgCloudStatus::create(MODULE_MSG_TRANSLATOR_ID, out);
// }

#include "msg_translators.h"
#include "ModuleMsgTranslator.h"  // for MODULE_MSG_TRANSLATOR_ID (used in create() calls)

// ---------------------------------------------------------------------------
// Translator implementations
// (Add application-specific functions below)
// ---------------------------------------------------------------------------
