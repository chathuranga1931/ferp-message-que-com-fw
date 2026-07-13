#ifndef _SYS_TYPES_H_
#define _SYS_TYPES_H_

#define ESP_ERROR_GOTO(ret, line, func) \
    {                                   \
        ret = (func);                   \
        if ((ret != ESP_OK))            \
            goto line;                  \
    }

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __cplusplus
}
#endif

#endif // _SYS_TYPES_H_