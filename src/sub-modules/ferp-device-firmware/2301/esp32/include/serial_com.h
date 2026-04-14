/*******************************************************************************
*                                SPLat Controls                                *
*                          Product Development Group                           *
*                            Melbourne,  AUSTRALIA                             *
*                            http://www.splatco.com                            *
********************************************************************************
   DESCRIPTION:
   API for the system settings.


********************************************************************************
*           Copyright (c) 2021 SPLat Controls. All rights reserved.            *
*  SPLatOS is free for commercial use on SPLat hardware and also free for not  *
* for profit use on non-SPLat hardware.  Commercial use either in whole or in  *
* part on non-SPLat hardware requires permission from SPLat Controls Pty Ltd.  *
*                                                                              *
* SPLatOS and associated documentation and tools are provided "as is", without *
*  warranty of any kind, express or implied, including but not limited to the  *
*     warranties of merchantability, fitness for a particular purpose and      *
*   non-infringement.  In no event shall the authors or copyright holders be   *
*  liable for any claim, damages or other liability, whether in an action of   *
* contract, tort or otherwise, arising from, out of or in connection with the  *
*            software or the use or other dealings in the software.            *
*                                                                              *
*  The above copyright notice and this permission notice shall be included in  *
*              all copies or substantial portions of the Software.             *
*******************************************************************************/

// must be first

/*==============================================================================

    INCLUDES

==============================================================================*/
#pragma once

#include <SmingCore.h>

/*==============================================================================

    DEFINES

==============================================================================*/

/*==============================================================================

    TYPES

==============================================================================*/



typedef union __attribute__((packed))
{
    uint64_t u64int;
    int64_t s64int;
    double dbl;
    uint8_t ab[8];

    // to keep in modbus memory
    bool bit0;
    bool bits[64];
    uint8_t u8int;
    int8_t s8int;
    uint16_t u16int;
    int16_t s16int;
    uint32_t u32int;
    int32_t s32int;
    float flt;
} tu8Bytes_t;

/*==============================================================================

    STATIC VARIABLES DECLARATIONS

==============================================================================*/

/*==============================================================================

    GLOBAL VARIABLE DECLARATIONS

==============================================================================*/

/*==============================================================================

    STATIC FUNCTION DECLARATIONS

==============================================================================*/

/*==============================================================================

    FUNCTION DECLARATIONS

==============================================================================*/


uint16_t DT_wCRC16Calc(uint16_t wCRC, const uint8_t *kpvData, uint32_t lwLength);