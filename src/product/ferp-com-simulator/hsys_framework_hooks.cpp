// hsys_framework_hooks.cpp  — simulator product layer
//
// Previously held strong overrides routing hsys_log → pal_logger.
// FWK_LOG_* macros in hsys_log.h now call pal_logger_log() directly —
// no function hooks or overrides needed.

