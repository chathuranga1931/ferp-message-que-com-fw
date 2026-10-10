
#include "user_config.h"
#include "pal_logger.h"


#define LIST_MGR_LOG_EN    true    // list_manager.c — list manager middleware used by retransmission manager
#define LIST_MGR_LOG_DEBUG(fmt, ...) LOG_MSG_DEBUG(LIST_MGR_LOG_EN, fmt, ##__VA_ARGS__)
#define LIST_MGR_LOG_ERROR(fmt, ...) LOG_MSG_ERROR(LIST_MGR_LOG_EN, fmt, ##__VA_ARGS__)