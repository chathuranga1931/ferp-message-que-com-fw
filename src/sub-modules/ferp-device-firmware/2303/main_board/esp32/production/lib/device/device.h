#ifndef _DEVICE_H_
#define _DEVICE_H_

#ifdef __cplusplus
extern "C" {
#endif

void initBoard(void(*pwr_dw_cb)(void));

void initDisplay();

#ifdef __cplusplus
}
#endif

#endif //_DEVICE_H_