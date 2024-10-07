/******************************************************************************
* File Name:   octobertech.h
*
* Description: This file contains the function prototypes and constants used
*   in octobertech.c.
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

#ifndef IFXGESTURE_PREPROCESS_OCTOBERTECH_H_
#define IFXGESTURE_PREPROCESS_OCTOBERTECH_H_
#define max(x,y) (((x) >= (y)) ? (x) : (y))

#include "preprocess.h"

typedef struct {
    uint16_t range_bin;
    uint16_t doppler_bin;
    float azimuth;
    float elevation;
    float value;
} slim_algo_detection;

typedef struct {
    uint16_t range_bin;
    uint16_t dummy;
    float doppler_bin;
    float azimuth;
    float elevation;
    float value;
} super_slim_algo_detection;

typedef struct {
    bool success;
    slim_algo_detection detection;
} slim_algo_output;

typedef struct {
    bool success;
    super_slim_algo_detection detection;
} super_slim_algo_output;
/*Structure to hold intermediate arrays for the octobertech slim_algo 
* processing. Use `new_preproc_octobertech_work_arrays()` to create an
* instance, and `free_preproc_octobertech_work_arrays()` to free up the
* arrays. */
typedef struct {
    /* Half frame (hfr): n_channels * n_chirps * n_range_bins */
    ifx_cf64_t *x_range;
    ifx_cf64_t *x_range_keep;
    ifx_f32_t *x_range_abs;
    /* Image (img): n_chirps * n_range_bins */
    ifx_f32_t *x_range_abs_mean;
    /* Chan. x chirps (cch): n_channels * n_chirps */
    ifx_cf64_t *x_range_slice;
    ifx_cf64_t *x_doppler;
    ifx_f32_t *x_doppler_abs;
    /* Chirps (chr): n_chirps */
    ifx_f32_t *doppler_window;
    ifx_f32_t *doppler_profile;
    /* Range bins (rbn): n_range_bins */
    ifx_f32_t *range_profile;
    /* Samples (smp): n_samples */
    ifx_f32_t *range_window;
} preproc_octobertech_work_arrays;

preproc_octobertech_work_arrays
new_preproc_octobertech_work_arrays(frame_cfg *f_cfg);

void free_preproc_octobertech_work_arrays(
    preproc_octobertech_work_arrays *arrays
);

void slim_algo(
    slim_algo_output *out, ifx_f32_t *x_frame, frame_cfg *f_cfg,
    uint16_t min_range_bin, preproc_octobertech_work_arrays *arr
);
uint32_t filter_range_profile(ifx_f32_t *range_profile, int32_t len, uint32_t peak_range);

void super_slim_algo(
    super_slim_algo_output *out, ifx_f32_t *x_frame, frame_cfg *f_cfg,
    uint16_t min_range_bin, preproc_octobertech_work_arrays *arr
);

void _get_range_profile_super_slim(
    ifx_cf64_t *x_range, preproc_octobertech_work_arrays *arr, frame_cfg *f_cfg,
    uint16_t min_range_bin
);
#endif
