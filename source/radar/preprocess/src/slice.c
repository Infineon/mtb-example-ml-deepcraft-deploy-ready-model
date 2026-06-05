/******************************************************************************
* File Name:   slice.c
*
* Description: This file handles slicing of the data.
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