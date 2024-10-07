/******************************************************************************
* File Name:   slice.c
*
* Description: This file handles slicing of the data.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/
#include "preprocess.h"

void slice_2d_row_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t row, uint16_t n_rows,
    uint16_t n_cols
) 
{
    memcpy(dst, src + row * n_cols, sizeof(ifx_cf64_t) * n_cols);
}

void slice_2d_col_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t col, uint16_t n_rows,
    uint16_t n_cols
) 
{
    for (uint16_t row = 0; row < n_rows; ++row)
    {
        *dst = src[row * n_cols + col];
        ++dst;
    }
}

void slice_3d_row_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t row, uint16_t n_ch,
    uint16_t n_rows, uint16_t n_cols
)
{
    uint32_t row_size = sizeof(ifx_cf64_t) * n_cols;
    for (uint16_t ch = 0; ch < n_ch; ++ch)
    {
        memcpy(dst, src + ch * n_rows * n_cols + row * n_cols, row_size);
        dst += n_cols;
    }
}

void slice_3d_col_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t col, uint16_t n_ch,
    uint16_t n_rows, uint16_t n_cols
)
{
    for (uint16_t ch = 0; ch < n_ch; ++ch)
    {
        for (uint16_t row = 0; row < n_rows; ++row)
        {
            *dst = src[ch * n_rows * n_cols + row * n_cols + col];
            ++dst;
        }
    }
}