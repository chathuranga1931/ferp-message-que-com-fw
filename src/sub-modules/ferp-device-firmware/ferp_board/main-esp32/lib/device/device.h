#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

void initBoard(void(*pwr_dw_cb)(void*), void *arg);
bool getBoardMetaData(board_meta_data_t *data);
bool setBoardMetaData(board_meta_data_t *data);
bool getDeviceMetaData(device_meta_data_t *data);
bool setDeviceMetaData(device_meta_data_t *data);

#ifdef __cplusplus
}
#endif

#endif //_DEVICE_H_