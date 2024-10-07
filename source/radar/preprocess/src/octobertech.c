/******************************************************************************
* File Name:   octobertech.c
*
* Description: This file does feature extraction from the preprocessed data.
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

#include "ifx_sensor_dsp.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "octobertech.h"
#include "preprocess.h"
#include "windows.h"
#include "math.h"
#ifdef __cplusplus
}
#endif

/*******************************************************************************
* Function Name: new_preproc_octobertech_work_arrays
********************************************************************************
* Summary:
* Instantiates a new struct of intermediate arrays and FFT windows for
*  the `slim_algo`.
*
* Parameters:
*  f_cfg  : Frame configuration.
*
* Return:
* structure with pre-allocated arrays
*
*******************************************************************************/
preproc_octobertech_work_arrays
new_preproc_octobertech_work_arrays(frame_cfg *f_cfg)
{
    uint32_t len_hfr = f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_range_bins;
    uint32_t len_img = f_cfg->n_chirps * f_cfg->n_range_bins;
    uint32_t len_cch = f_cfg->n_channels * f_cfg->n_chirps;
    uint32_t sz_f = sizeof(ifx_f32_t);
    uint32_t sz_c = sizeof(ifx_cf64_t);
    preproc_octobertech_work_arrays arrays = {
        .x_range = (ifx_cf64_t *)malloc(sz_c * len_hfr),
        .x_range_keep = (ifx_cf64_t *)malloc(sz_c * len_hfr),
        .x_range_abs = (ifx_f32_t *)malloc(sz_f * len_hfr),
        .x_range_abs_mean = (ifx_f32_t *)malloc(sz_f * len_img),
        .x_range_slice = (ifx_cf64_t *)malloc(sz_c * len_cch),
        .x_doppler = (ifx_cf64_t *)malloc(sz_c * len_cch),
        .x_doppler_abs = (ifx_f32_t *)malloc(sz_f * len_cch),
        .doppler_profile = (ifx_f32_t *)malloc(sz_f * f_cfg->n_chirps),
        .doppler_window = (ifx_f32_t *)malloc(sz_f * f_cfg->n_chirps),
        .range_profile = (ifx_f32_t *)malloc(sz_f * f_cfg->n_range_bins),
        .range_window = (ifx_f32_t *)malloc(sz_f * f_cfg->n_samples)
    };
    if  (f_cfg->n_chirps>=16)
    {
        get_window(&WINDOWS.kaiser_b25, arrays.doppler_window, f_cfg->n_chirps);
    }
    get_window(&WINDOWS.hann, arrays.range_window, f_cfg->n_samples);
    return arrays;
}

/* Frees the intermediate `slim_algo` arrays. */
void free_preproc_octobertech_work_arrays(
    preproc_octobertech_work_arrays *arrays
)
{
    free(arrays->x_range);
    free(arrays->x_range_abs);
    free(arrays->x_range_abs_mean);
    free(arrays->x_range_slice);
    free(arrays->x_doppler);
    free(arrays->x_doppler_abs);
    free(arrays->doppler_profile);
    free(arrays->doppler_window);
    free(arrays->range_profile);
    free(arrays->range_window);
}


/*******************************************************************************
* Function Name: _get_range_profile
********************************************************************************
* Summary:
* Computes range profile from range image.
*
* Parameters:
*  x_range       : Range images (per channel).
*  arr           : Intermediate working arrays. Result of this function is
*  stored in `arr->range_profile`.
*  min_range_bin :  The closest range bin until which the values are
*  ignored for the profile calculation.
*
*******************************************************************************/
static void _get_range_profile(
    ifx_cf64_t *x_range, preproc_octobertech_work_arrays *arr, frame_cfg *f_cfg,
    uint16_t min_range_bin
)
{
    uint16_t size = f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_range_bins;
    arm_cmplx_mag_f32((float32_t *)x_range, (float32_t *)arr->x_range_abs, size);
    mean_rdi_channel_f32(arr->x_range_abs, arr->x_range_abs_mean, f_cfg);
    for (int idx_rb = min_range_bin; idx_rb < f_cfg->n_range_bins; ++idx_rb) {
        ifx_f32_t sum = 0;
        // 1st chirp is weird -- amplitudes look too high compared to other chirps.
        // We ignore it for the range_profile calculation.
        for (int idx_chirp = 1; idx_chirp < f_cfg->n_chirps; ++idx_chirp) {
            sum += arr->x_range_abs_mean[(idx_chirp * f_cfg->n_range_bins) + idx_rb];
        }
        arr->range_profile[idx_rb - min_range_bin] = sum / (f_cfg->n_chirps - 1);
    }
}

/*******************************************************************************
* Function Name: _get_range_profile_super_slim
********************************************************************************
* Summary:
* Computes range profile from range image.
*
* Parameters:
*  x_range       : Range images (per channel).
*  arr           : Intermediate working arrays. Result of this function is
*  stored in `arr->range_profile`.
*  min_range_bin :  The closest range bin until which the values are
*  ignored for the profile calculation.
*
*******************************************************************************/
void _get_range_profile_super_slim(
    ifx_cf64_t *x_range, preproc_octobertech_work_arrays *arr, frame_cfg *f_cfg,
    uint16_t min_range_bin
)
{
    uint16_t size = f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_range_bins;
    arm_cmplx_mag_f32((float32_t *)x_range, (float32_t *)arr->x_range_abs, size);
    mean_rdi_channel_f32(arr->x_range_abs, arr->x_range_abs_mean, f_cfg);
    arm_fill_f32(0,arr->range_profile,f_cfg->n_range_bins);
    for (int idx_rb = min_range_bin; idx_rb < f_cfg->n_range_bins; ++idx_rb) {
        ifx_f32_t sum = 0;
        for (int idx_chirp = 0; idx_chirp < f_cfg->n_chirps; ++idx_chirp) {
            sum += arr->x_range_abs_mean[(idx_chirp * f_cfg->n_range_bins) + idx_rb];
        }
        arr->range_profile[idx_rb - min_range_bin] = sum / (f_cfg->n_chirps);
    }
}

/*******************************************************************************
* Function Name: _get_single_range_bin_doppler
********************************************************************************
* Summary:
* Computes a doppler FFT on for a single range bin.
*
* Parameters:
*  x_range       : Range images (per channel).
*  arr           : Intermediate arrays. Output of this function is stored in
* `arr->x_doppler`.
*  range_bin :  Range bin for which the doppler FFT is
*  performed.
*
*******************************************************************************/
static void _get_single_range_bin_doppler(
    ifx_cf64_t *x_range, preproc_octobertech_work_arrays *arr, uint32_t range_bin,
    frame_cfg *f_cfg
)
{
    slice_3d_col_cf64(
        x_range, arr->x_range_slice, range_bin, f_cfg->n_channels, f_cfg->n_chirps,
        f_cfg->n_range_bins
    );
    for (uint16_t idx_ch = 0; idx_ch < f_cfg->n_channels; ++idx_ch) {
        (void)ifx_doppler_cfft_f32(
            (cfloat32_t *)(arr->x_range_slice + idx_ch * f_cfg->n_chirps),
            (cfloat32_t *)(arr->x_doppler + idx_ch * f_cfg->n_chirps), false,
            arr->doppler_window, 1, f_cfg->n_chirps
        );
        fftshift_cf64(arr->x_doppler + idx_ch * f_cfg->n_chirps, f_cfg->n_chirps);
    }
}

/*******************************************************************************
* Function Name: _get_doppler_profile
********************************************************************************
* Summary:
* Computes Doppler profile from Doppler spectra.
*
* Parameters:
*  x_doppler       : Doppler spectra (per channel) of a single range bin.
*  arr           : Intermediate arrays. Output of this function is stored in
*  `arr->doppler_profile`.
*
*******************************************************************************/
static void _get_doppler_profile(
    ifx_cf64_t *x_doppler, preproc_octobertech_work_arrays *arr, frame_cfg *f_cfg
)
{
    arm_cmplx_mag_f32(
        (float32_t *)x_doppler, (float32_t *)arr->x_doppler_abs,
        f_cfg->n_channels * f_cfg->n_chirps
    );
    for (uint16_t idx_chirp = 0; idx_chirp < f_cfg->n_chirps; ++idx_chirp) {
        ifx_f32_t sum = 0;
        for (int idx_ch = 0; idx_ch < f_cfg->n_channels; ++idx_ch) {
            sum += arr->x_doppler_abs[idx_ch * f_cfg->n_chirps + idx_chirp];
        }
        arr->doppler_profile[idx_chirp] = sum / f_cfg->n_channels;
    }
}

uint32_t filter_range_profile(ifx_f32_t *range_profile, int32_t len, uint32_t peak_range)
{
    /* extracts local maximum, if one is found */
    ifx_f32_t threshold;
    float32_t weights [] = {1.33830625e-04, 4.43186162e-03, 5.39911274e-02, 2.41971446e-01, 3.98943469e-01, 2.41971446e-01, 5.39911274e-02, 4.43186162e-03, 1.33830625e-04};
    threshold = max(0.1*range_profile[peak_range], 1e-4);

    float32_t* conv_out = malloc(sizeof(float32_t) * (len + 9 - 1));

    arm_conv_f32(range_profile, (uint32_t)len, weights, 9, conv_out);

    float32_t *p_conv_out = &conv_out[4];

    for (int i=0; i<len; i++)if (p_conv_out[i]<threshold) p_conv_out[i]=0;
    int32_t peak_idx = -1;
    for (int32_t i=1; i<len-1; i++) {
        if ((p_conv_out[i-1] < p_conv_out[i]) && (p_conv_out[i+1] < p_conv_out[i])) {
            peak_idx = i;
            break;
        }
    }

    free(conv_out);

    if (peak_idx>-1) {
        return (uint32_t)peak_idx;
    } else return (uint32_t)peak_range;

}

/*******************************************************************************
* Function Name: slim_algo
********************************************************************************
* Summary:
* Extracts hand features for gesture recognition.
*
* Parameters:
*  out           : out Preprocessing algorithm output containing detected hand
*  features.
*  x_frame       : Raw radar frame.
*  f_cfg         :  Frame configuration.
*  min_range_bin : The closest range bin to use for hand detection. Closer
/// ranges are ignored.
*  arr           : Intermediate pre-allocated arrays. Use
*  `new_preproc_octobertech_work_arrays()` to create the instance of this
*  struct with pre-allocated arrays. Reuse the same struct for processing all
*  frames.
*
*******************************************************************************/
void slim_algo(
    slim_algo_output *out, ifx_f32_t *x_frame, frame_cfg *f_cfg,
    uint16_t min_range_bin, preproc_octobertech_work_arrays *arr
)
{
    /* Build range images, suppress static targets, compute a range profile */
    build_complex_range_image(x_frame, arr->x_range, f_cfg, arr->range_window);
    remove_mean_3d_cf64(
        arr->x_range, 1, f_cfg->n_channels, f_cfg->n_chirps, f_cfg->n_range_bins
    );
    _get_range_profile(arr->x_range, arr, f_cfg, min_range_bin);

    /* Find peak in the range profile - consider it as range to the hand */
    uint32_t idx_peak_range;
    ifx_f32_t val_peak_range;
    arm_max_f32(
        arr->range_profile, f_cfg->n_range_bins - min_range_bin, &val_peak_range,
        &idx_peak_range
    );

    idx_peak_range = filter_range_profile(arr->range_profile, f_cfg->n_range_bins-min_range_bin, idx_peak_range);

    idx_peak_range += min_range_bin;

    /* Compute Doppler spectrum for the peak range bin (for each channel), then
    *   make a Doppler profile.*/
    _get_single_range_bin_doppler(arr->x_range, arr, idx_peak_range, f_cfg);
    _get_doppler_profile(arr->x_doppler, arr, f_cfg);

    /* Find peak in the Doppler profile - consider it as velocity of the hand */
    uint32_t idx_peak_doppler;
    ifx_f32_t val_peak_doppler;
    arm_max_f32(
        arr->doppler_profile, f_cfg->n_chirps, &val_peak_doppler, &idx_peak_doppler
    );

    /* Extract phases from Doppler spectrum of each channel */
    float phases[3];
    for (int i = 0; i < 3; ++i)
    {
        ifx_f32_t re =
            arr->x_doppler[i * f_cfg->n_chirps + idx_peak_doppler].data[0];
        ifx_f32_t im =
            arr->x_doppler[i * f_cfg->n_chirps + idx_peak_doppler].data[1];
        if (angle(re, im, phases + i) != ARM_MATH_SUCCESS) {
            out->success = false;
            return;
        }
    }

    float azimuth = phase_monopulse(phases[2], phases[0]);
    float elevation = phase_monopulse(phases[2], phases[1]);
    /* Phase correction based on Signify measurements */
    azimuth = azimuth + deg2rad(8.0);
    elevation = elevation + deg2rad(24.0);

    out->success = true;
    out->detection = (slim_algo_detection
    )
    {
        .range_bin = idx_peak_range,
        .doppler_bin = idx_peak_doppler,
        .azimuth = azimuth,
        .elevation = elevation,
        .value = val_peak_doppler
    };
}

void super_slim_algo(
    super_slim_algo_output *out, ifx_f32_t *x_frame, frame_cfg *f_cfg,
    uint16_t min_range_bin, preproc_octobertech_work_arrays *arr
)
{
    /* Build range images, suppress static targets, compute a range profile */
    build_complex_range_image(x_frame, arr->x_range, f_cfg, arr->range_window);
    memcpy(arr->x_range_keep, arr->x_range, f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_range_bins *sizeof(ifx_cf64_t));
    remove_mean_3d_cf64(
        arr->x_range, 1, f_cfg->n_channels, f_cfg->n_chirps, f_cfg->n_range_bins
    );
    _get_range_profile_super_slim(arr->x_range, arr, f_cfg, min_range_bin);
    /* Find peak in the range profile - consider it as range to the hand */
    uint32_t idx_peak_range;
    ifx_f32_t val_peak_range;
    arm_max_f32(
        arr->range_profile, f_cfg->n_range_bins - min_range_bin, &val_peak_range,
        &idx_peak_range
    );
    idx_peak_range = filter_range_profile(
                         arr->range_profile, f_cfg->n_range_bins - min_range_bin, idx_peak_range
                     );

    idx_peak_range += min_range_bin;
    float** phases;
    phases = (float**)malloc(f_cfg->n_channels * sizeof(float*));
    for (int i = 0; i < f_cfg->n_channels; i++) {
        phases[i] = (float*)malloc(f_cfg->n_chirps * sizeof(float));
    }


    for (int a = 0; a < f_cfg->n_channels; ++a)
    {
        ifx_cf64_t *this_antena =
            arr->x_range_keep + (f_cfg->n_range_bins * f_cfg->n_chirps * a);
        for (int c = 0; c < f_cfg->n_chirps; ++c)
        {
            ifx_f32_t re = this_antena[c*f_cfg->n_range_bins + idx_peak_range].data[0];
            ifx_f32_t im = this_antena[c*f_cfg->n_range_bins + idx_peak_range].data[1];
            if (angle(re, im, &phases[a][c]) != ARM_MATH_SUCCESS)
            {
                out->success = false;

                /* Free the memory for each row */
                for (int i = 0; i < f_cfg->n_channels; i++)
                {
                    free(phases[i]);
                }

                /* Free the memory for the array itself */
                free(phases);

                return;
            }
        }
    }
    float doppler = 0;
    for (int a = 0; a < f_cfg->n_channels; ++a)
    {
        doppler += get_phase_difference(phases[a][1], phases[a][0]);
    }
    doppler = doppler / f_cfg->n_channels;

    float azimuth = 0;
    float elevation = 0;
    for (int c = 0; c < f_cfg->n_chirps; ++c)
    {
        azimuth += phase_monopulse(phases[2][c], phases[0][c]);
        elevation += phase_monopulse(phases[2][c], phases[1][c]);
    }
    azimuth = azimuth / f_cfg->n_chirps;
    elevation = elevation / f_cfg->n_chirps;

    out->success = true;
    out->detection = (super_slim_algo_detection
    )
    {
        .range_bin = idx_peak_range,
        .doppler_bin = doppler,
        .azimuth = azimuth,
        .elevation = elevation,
        .value = val_peak_range
    };

    /* Free the memory for each row */
    for (int i = 0; i < f_cfg->n_channels; i++)
    {
        free(phases[i]);
    }

    /* Free the memory for the array itself */
    free(phases);
}

