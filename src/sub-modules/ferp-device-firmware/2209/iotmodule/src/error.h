
#ifndef __ERROR_H
#define __ERROR_H

enum ret_t{

    ret_Success = 0,

    /* Application Errors */
    ret_Err_App = 10,
    ret_Err_App_NoConfigFile,
    ret_Err_App_NoInternet,
    ret_Err_App_TimeOut,
    ret_Err_App_SubmitCloud,
    ret_Err_App_SubmitPrinter,

    /* Platform Error */
    ret_Err_Pltfrm = 40,
    ret_Err_Pltfrm_NoConfigFile,

    /* Utility Error */
    ret_Err_Utlty = 60,

    /* Harware Errors */
    ret_Err_Hdware = 80,
    ret_Err_Hdware_GetLocalTime,

    /* General */
    ret_Err_Gen = 100,
    ret_Err_Gen_NullP,
    ret_Err_Gen_SmallBuffer,
    ret_Err_Gen_InvalidParam,

};

#endif //__ERROR_H