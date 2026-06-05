/******************************************************************************
* File Name: resource_map.h
*
* Description: This file defines the SPI and GPIO pin map for all the supported kits.
*
* Related Document: See README.md
*
*
*******************************************************************************
* (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

#ifndef RESOURCE_MAP_H_
#define RESOURCE_MAP_H_
#include "cybsp.h"

#ifdef GESTURE_MODEL

#define LED_RGB_RED                           CYBSP_USER_LED1
#define LED_RGB_GREEN                         CYBSP_USER_LED2

#define PIN_XENSIV_BGT60TRXX_SPI_SCLK         CYBSP_RSPI_CLK
#define PIN_XENSIV_BGT60TRXX_SPI_MOSI         CYBSP_RSPI_MOSI
#define PIN_XENSIV_BGT60TRXX_SPI_MISO         CYBSP_RSPI_MISO
#define PIN_XENSIV_BGT60TRXX_SPI_CSN          CYBSP_RSPI_CS
#define PIN_XENSIV_BGT60TRXX_IRQ              CYBSP_RSPI_IRQ
#define PIN_XENSIV_BGT60TRXX_RSTN             CYBSP_RXRES_L

#endif

#endif 
