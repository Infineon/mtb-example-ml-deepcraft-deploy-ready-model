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
