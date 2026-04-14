
#ifndef _HONGYNAG_8DIGIT_H_
#define _HONGYNAG_8DIGIT_H_

#include "esp_err.h"
#include "dis_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void init_hongyang_display();
    void dis1_copy_data(const dis_capture_t *dis);
#if DIS2_CAPTURE_ENABLE
    void dis2_copy_data(const dis_capture_t *dis);
#endif

#ifdef __cplusplus
}
#endif

#endif /*_HONGYNAG_8DIGIT_H_*/