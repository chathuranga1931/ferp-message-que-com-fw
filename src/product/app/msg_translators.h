// msg_translators.h
//
// Declares every translator function referenced in the translation table
// defined in app.cpp (k_translator_table[]).
//
// Conventions:
//   - Function names follow the pattern: xlat_<in_msg>_to_<out_msg>
//   - All functions share the msg_translator_fn_t signature:
//       hsys_msg_t *fn(hsys_module_id_t in_src,
//                      const hsys_msg_t *in_msg,
//                      hsys_msg_id_t     out_msg_id,
//                      hsys_module_id_t  out_dest)
//   - Return a newly-allocated hsys_msg_t* to publish, or nullptr to suppress.
//   - Implementations live in msg_translators.cpp.
//
// To add a new translator:
//   1. Declare the function here.
//   2. Implement it in msg_translators.cpp.
//   3. Add a row to k_translator_table[] in app.cpp.

#pragma once

#include "hsys_msg.h"
#include "hsys_types.h"

// ---------------------------------------------------------------------------
// Translator function declarations
//
// Add one line per translator used in the table.
// Example:
//
//   hsys_msg_t *xlat_fuel_pumped_to_cloud_status(hsys_module_id_t in_src,
//                                                const hsys_msg_t *in_msg,
//                                                hsys_msg_id_t     out_msg_id,
//                                                hsys_module_id_t  out_dest);
// ---------------------------------------------------------------------------

// (Application-specific translator declarations go here)
