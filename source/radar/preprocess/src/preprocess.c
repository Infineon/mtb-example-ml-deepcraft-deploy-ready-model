/******************************************************************************
* File Name:   preprocess.c
*
* Description: This file does the preprocessing of radar sensor data.
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

#ifdef __cplusplus
extern "C" {
#endif
#include "preprocess.h"
#ifdef __cplusplus
}
#endif
#include "arm_math_types.h"
#include "dsp/basic_math_functions.h"
#include "dsp/complex_math_functions.h"
#include "dsp/fast_math_functions.h"
#include "dsp/matrix_functions.h"
#include "dsp/statistics_functions.h"
#include "dsp/transform_functions.h"
#include "ifx_sensor_dsp.h"
#include "windows.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ARMCC_VERSION)

#pragma message("Compiling with ARM Compiler: Custome definition of __devsc3 is included")

#include <complex.h>

_Complex float  __divsc3(float a, float b, float c, float d) {
    float denom, ratio, x, y;

    if (fabsf(c) < fabsf(d)) {
        ratio = c / d;
        denom = c * ratio + d;
        x = (a * ratio + b) / denom;
        y = (b * ratio - a) / denom;
    }
    else {
        ratio = d / c;
        denom = d * ratio + c;
        x = (b * ratio + a) / denom;
        y = (b - a * ratio) / denom;
    }

    return x + y * I;
}

#endif


void rfft_f32(ifx_f32_t *x, ifx_cf64_t *out, uint16_t n_samples)
{
    /* Real FFT. Input and output are different buffers. */ 
    arm_rfft_fast_instance_f32 rfft = {0};
    arm_rfft_fast_init_f32(&rfft, n_samples);
    arm_rfft_fast_f32(&rfft, (float32_t *)x, (float32_t *)out, 0);
}

void cfft_f32(ifx_cf64_t *x, uint16_t n_samples)
{
    // Complex FFT. Inplace
    arm_cfft_instance_f32 cfft = {0};
    arm_cfft_init_f32(&cfft, n_samples);
    arm_cfft_f32(&cfft, (float32_t *)x, 0, 1);
}

/*******************************************************************************
* Function Name: fftshift_cf64
********************************************************************************
* Summary:
* Shifts FFT spectrum (in place) to make zero frequencies in the center.
* Note: in 2D case shifting along row axis can be done with the same
* function by providing `len=num_rows*num_cols`.
*
* Parameters:
*  in  : Input array.
*  len : Number of elements.
*
*******************************************************************************/
void fftshift_cf64(ifx_cf64_t *in, uint32_t len)
{
    assert(len % 2 == 0);
    int half = len / 2;
    ifx_cf64_t tmp;
    for (int i = 0; i < half; ++i) {
        // Swap elements at `i` and `i+half`
        tmp = in[i];
        in[i] = in[i + half];
        in[i + half] = tmp;
    }
}

void range_transform(ifx_f32_t *x, ifx_cf64_t *out, range_transform_cfg *cfg)
{
    uint32_t status = ifx_range_fft_f32(
                          (float32_t *)x, (cfloat32_t *)out, cfg->remove_mean,
                          (float32_t *)cfg->window, cfg->n_samples, cfg->n_chirps
                      );
    if (status == IFX_SENSOR_DSP_ARGUMENT_ERROR)
    {
        abort();
    }

    for (int chirp = 0; chirp < cfg->n_chirps; chirp++)
    {
        ((ifx_cf64_t *)out + (chirp * cfg->n_samples / 2))->data[1] = 0.0;
    }
}

void range_doppler_transform(
    ifx_f32_t *x, ifx_cf64_t *out, range_doppler_transform_cfg *cfg
)
{
    cfloat32_t* range_array = (cfloat32_t*)malloc(sizeof(cfloat32_t) * (cfg->n_chirps * cfg->n_samples / 2));
    cfloat32_t* doppler_array = (cfloat32_t*)malloc(sizeof(cfloat32_t) * (cfg->n_chirps * cfg->n_samples / 2));

    range_transform_cfg range_cfg = {
        .n_samples = cfg->n_samples,
        .n_chirps = cfg->n_chirps,
        .remove_mean = cfg->range_remove_mean,
        .window = cfg->range_window
    };
    range_transform(x, (ifx_cf64_t *)range_array, &range_cfg);

    uint32_t status = ifx_doppler_cfft_f32(
                          range_array, doppler_array, cfg->doppler_remove_mean, cfg->doppler_window,
                          cfg->n_samples / 2, cfg->n_chirps
                      );
    if (status == IFX_SENSOR_DSP_ARGUMENT_ERROR)
    {
        abort();
    }

    arm_matrix_instance_f32 doppler_matrix =
    {
        cfg->n_samples / 2, cfg->n_chirps, (float32_t *)doppler_array
    };
    arm_matrix_instance_f32 out_matrix =
    {
        cfg->n_chirps, cfg->n_samples / 2, (float32_t *)out
    };
    (void)arm_mat_cmplx_trans_f32(&doppler_matrix, &out_matrix);
    fftshift_cf64((ifx_cf64_t *)out, cfg->n_chirps * cfg->n_samples / 2);

    free(range_array);
    free(doppler_array);
}

void build_complex_range_image(
    ifx_f32_t *raw_frame, ifx_cf64_t *out, frame_cfg *f_cfg, ifx_f32_t *window
)
{
    uint16_t src_idx = 0;
    uint16_t dst_idx = 0;
    range_transform_cfg range_transf_cfg =
    {
        .n_chirps = f_cfg->n_chirps,
        .n_samples = f_cfg->n_samples,
        .remove_mean = true,
        .window = window
    };

    uint16_t frame_size = f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_samples;
    arm_scale_f32(
        (float32_t *)raw_frame, 1.0 / (float32_t)ADC_NORMALIZATION,
        (float32_t *)raw_frame, frame_size
    );

    for (int ch = 0; ch < f_cfg->n_channels; ++ch)
    {
        range_transform(raw_frame + src_idx, out + dst_idx, &range_transf_cfg);
        src_idx += f_cfg->n_chirps * f_cfg->n_samples;
        dst_idx += f_cfg->n_chirps * f_cfg->n_range_bins;
    }
}

void build_complex_rdi(
    ifx_f32_t *raw_frame, ifx_cf64_t *out, frame_cfg *f_cfg
)
{
    uint16_t src_idx = 0;
    uint16_t dst_idx = 0;
    ifx_f32_t *range_window = (ifx_f32_t*) malloc(sizeof(ifx_f32_t) * f_cfg->n_samples);
    get_window(&WINDOWS.hann, range_window, f_cfg->n_samples);
    ifx_f32_t *doppler_window = (ifx_f32_t*) malloc(sizeof(ifx_f32_t) * f_cfg->n_chirps);
    get_window(&WINDOWS.kaiser_b25, doppler_window, f_cfg->n_chirps);
    
    range_doppler_transform_cfg rd_cfg =
    {
        .n_chirps = f_cfg->n_chirps,
        .n_samples = f_cfg->n_samples,
        .range_remove_mean = true,
        .doppler_remove_mean = true,
        .range_window = range_window,
        .doppler_window = doppler_window
    };
    uint16_t frame_size = f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_samples;
    arm_scale_f32(
        (float32_t *)raw_frame, 1.0 / (float32_t)ADC_NORMALIZATION,
        (float32_t *)raw_frame, frame_size
    );

    for (int ch = 0; ch < f_cfg->n_channels; ++ch)
    {
        range_doppler_transform(raw_frame + src_idx, out + dst_idx, &rd_cfg);
        src_idx += f_cfg->n_chirps * f_cfg->n_samples;
        dst_idx += f_cfg->n_chirps * f_cfg->n_range_bins;
    }

    free(range_window);
    free(doppler_window);
}

void estimate_human(
    ifx_f32_t *frame_abs_rdi, frame_cfg *f_cfg, estimate_human_cfg *cfg
)
{
    uint16_t peak_range = -1;
    ifx_f32_t peak_val = -1.0;
    /* Look at two rows around 0 velocity (excluding the one in between == 
    n_chirps/2 + 0) */
    uint16_t search_rows[] = {
        (f_cfg->n_chirps / 2) - 1, (f_cfg->n_chirps / 2) + 1
    };
    uint16_t row;
    for (int row_idx = 0; row_idx < 2; ++row_idx)
    {
        for (int col = cfg->position_min; col < f_cfg->n_range_bins; ++col)
        {
            row = search_rows[row_idx];
            if (frame_abs_rdi[row * f_cfg->n_range_bins + col] > peak_val)
            {
                peak_val = frame_abs_rdi[row * f_cfg->n_range_bins + col];
                peak_range = col;
            }
        }
    }
    if (cfg->position_current < 0)
    {
        cfg->position_current = (ifx_f32_t)peak_range;
    } else
    {
        cfg->position_current = (1 - cfg->alpha) * cfg->position_current +
                                cfg->alpha * (ifx_f32_t)peak_range;
    }
}

ifx_status cmplx_image_transpose(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t num_rows, uint16_t num_cols
)
{
    arm_matrix_instance_f32 src_mat =
    {
        num_rows, num_cols, (float32_t *)src
    };
    arm_matrix_instance_f32 dst_mat =
    {
        num_rows, num_cols, (float32_t *)dst
    };

    return (ifx_status)arm_mat_cmplx_trans_f32(&src_mat, &dst_mat);
}

ifx_status
cmplx_frame_transpose(ifx_cf64_t *src, ifx_cf64_t *dst, frame_cfg *f_cfg)
{
    ifx_status status=0;
    uint16_t offset = 0;
    for (int i = 0; i < f_cfg->n_channels; ++i)
    {
        status = cmplx_image_transpose(
                     src + offset, dst + offset, f_cfg->n_chirps, f_cfg->n_range_bins
                 );
        if (status != ARM_MATH_SUCCESS)
        {
            return status;
        }
        offset += f_cfg->n_chirps * f_cfg->n_range_bins;
    }
    return status;
}

void mean_rdi_channel_f32(
    ifx_f32_t *abs_rdi, ifx_f32_t *mean, frame_cfg *f_cfg
)
{

    uint16_t len = f_cfg->n_chirps * f_cfg->n_range_bins;
    for (int i = 0; i < len; ++i)
    {
        mean[i] = 0.0;
        for (int ch = 0; ch < f_cfg->n_channels; ++ch)
        {
            mean[i] += abs_rdi[ch * len + i];
        }
    }
    arm_scale_f32(
        (float32_t *)mean, 1.0 / f_cfg->n_channels, (float32_t *)mean, len
    );
}


uint16_t calculate_lower_range_limit(
    uint16_t roi_upper_limit, uint16_t band_max, uint16_t range_min
)
{
    int res = roi_upper_limit - band_max;
    if (res < range_min)
    {
        res = range_min;
    }
    return (uint16_t)res;
}

uint16_t calculate_upper_range_limit(
    ifx_f32_t position, uint16_t band_min, uint16_t band_offset,
    uint16_t range_min
)
{
    /* roundf different from np.rint for exactly .5 
    *  roundf(0.5) == 1.0 
    *  np.rint(0.5) == 0.0 
    *  we ignore it */
    uint16_t res = (uint16_t)rintf(position) + band_offset;
    if (res < range_min + band_min)
    {
        res = range_min + band_min;
    }
    return res;
}

void get_hand_roi(
    region *search_region, region *human_mask, frame_cfg *f_cfg,
    uint16_t lower_range_limit, uint16_t upper_range_limit, uint16_t guard_range,
    uint16_t guard_doppler
)
{

    search_region->row_start = 0;
    search_region->row_end = f_cfg->n_chirps;
    search_region->col_start = lower_range_limit;
    if (upper_range_limit > f_cfg->n_range_bins)
    {
        search_region->col_end = f_cfg->n_range_bins;
    } else
    {
        search_region->col_end = upper_range_limit;
    }

    uint16_t range_width = upper_range_limit - lower_range_limit;
    guard_range = (guard_range < range_width) ? guard_range : range_width;
    human_mask->col_start = search_region->col_end - guard_range;
    human_mask->col_end = search_region->col_end;
    human_mask->row_start = (f_cfg->n_chirps / 2) - guard_doppler;
    human_mask->row_end = (f_cfg->n_chirps / 2) + guard_doppler + 1;
}

void mask_hand_roi(
    const ifx_f32_t *mean_abs_rdi, ifx_f32_t *masked_out, const frame_cfg *f_cfg,
    const region *search_region, const region *human_mask
)
{
    /* Initialize `masked_out` with zeros, let the compiler optimize with memset
    *  if it wants */
    int offset;
    for (int row = 0; row < f_cfg->n_chirps; ++row)
    {
        for (int col = 0; col < f_cfg->n_range_bins; ++col)
        {
            offset = row * f_cfg->n_range_bins + col;
            *(masked_out + offset) = 0.0;
        }
    }
    /* Copy one-by-one values form the region of interest */
    for (int row = search_region->row_start; row < search_region->row_end;
            ++row)
    {
        for (int col = search_region->col_start; col < search_region->col_end;
                ++col)
        {
            offset = row * f_cfg->n_range_bins + col;
            *(masked_out + offset) = *(mean_abs_rdi + offset);
        }
    }
    /* Mask-out the human */
    for (int row = human_mask->row_start; row < human_mask->row_end; ++row)
    {
        for (int col = human_mask->col_start; col < human_mask->col_end; ++col)
        {
            offset = row * f_cfg->n_range_bins + col;
            *(masked_out + offset) = 0.0;
        }
    }
}

int compare_ifx_f32(const void *a, const void *b)
{
    if (*(ifx_f32_t *)a > *(ifx_f32_t *)b)
        return 1;
    if (*(ifx_f32_t *)a < *(ifx_f32_t *)b)
        return -1;
    return 0;
};

float get_background_level(
    const ifx_f32_t *masked_mean_abs_rdi, const frame_cfg *f_cfg
)
{
    /* Compute median of positive (non-zero in an absolute rdi) elements in the
    * region of interest. */
    int len = f_cfg->n_chirps * f_cfg->n_range_bins;
    ifx_f32_t* tmp = (ifx_f32_t*)malloc(sizeof(ifx_f32_t) * len);
    memcpy(tmp, masked_mean_abs_rdi, len * sizeof(ifx_f32_t));
    qsort((void *)tmp, len, sizeof(ifx_f32_t), compare_ifx_f32);
    /* Find index of first element greater than 0.0 */
    int idx;
    for (idx = 0; idx < len; ++idx)
    {
        if (tmp[idx] > 0.0)
        {
            break;
        }
    }
    if (idx == len)
    {
        free(tmp);
        /* No elements greater than 0.0 => return background_level == 0.0 */
        return 0.0;
    }
    /* Median of non-zero elements */
    int n_nonzero = len - idx;
    if (n_nonzero % 2 == 0)
    {
        float ret = (tmp[idx + n_nonzero / 2 - 1] + tmp[idx + n_nonzero / 2]) / 2;
        
        free(tmp);

        return ret;
    }

    float ret_val = tmp[idx + n_nonzero / 2];

    free(tmp);

    return ret_val;
}

void make_doppler_profile(
    const ifx_f32_t *mean_abs_rdi, ifx_f32_t *profile,
    const region *search_region, const frame_cfg *f_cfg
)
{
    /* Mean over range bins in the channel-combined-abs-rdi */
    uint16_t n_rows = search_region->row_end - search_region->row_start;
    uint16_t n_cols = search_region->col_end - search_region->col_start;
    uint16_t frame_row, frame_col;
    for (int local_row = 0; local_row < n_rows; ++local_row)
    {
        profile[local_row] = 0.0;
        frame_row = local_row + search_region->row_start;
        for (int local_col = 0; local_col < n_cols; ++local_col)
        {
            frame_col = local_col + search_region->col_start;
            profile[local_row] +=
                mean_abs_rdi[frame_row * f_cfg->n_range_bins + frame_col];
        }
    }
    arm_scale_f32(
        (float32_t *)profile, 1.0 / n_cols, (float32_t *)profile, n_rows
    );
}

typedef struct
{
    void *el;
    uint16_t idx;
} argsort_tuple;

int compare_argsort_tuples_ifx_f32_t(const void *a, const void *b)
{
    ifx_f32_t aa = *((ifx_f32_t *)(((argsort_tuple *)a)->el));
    ifx_f32_t bb = *((ifx_f32_t *)(((argsort_tuple *)b)->el));
    if (aa > bb)
    {
        return 1;
    }
    if (aa < bb)
    {
        return -1;
    }
    return 0;
}

void find_peaks(
    const ifx_f32_t *in, uint16_t *idx, uint16_t n_elements, uint16_t n_peaks
)
{
    /* `idx` array must be preallocated to fit `n_peaks` indices */

    argsort_tuple* indexed = (argsort_tuple*)malloc(sizeof(argsort_tuple) * n_elements);
    
    for (int i = 0; i < n_elements; ++i)
    {
        indexed[i].el = (void *)(in + i);
        indexed[i].idx = i;
    }
    qsort(
        indexed, n_elements, sizeof(argsort_tuple), compare_argsort_tuples_ifx_f32_t
    );
    for (int i = 0; i < n_peaks; ++i)
    {
        idx[i] = indexed[n_elements - i - 1].idx;
    }

    free(indexed);
}

void cluster_peaks(
    const uint16_t *peaks, peak_cluster *clusters, uint16_t n_peaks
)
{
    for (int i = 0; i < n_peaks; ++i)
    {
        clusters[i].n_elements = 0;
    }

    bool peak_assigned;
    uint16_t el;
    uint16_t n_el;

    for (int peak_idx = 0; peak_idx < n_peaks; ++peak_idx)
    {
        peak_assigned = false;
        for (int cluster_idx = 0; cluster_idx < n_peaks; ++cluster_idx)
        {
            for (int el_idx = 0; el_idx < clusters[cluster_idx].n_elements;
                    ++el_idx)
                {
                el = clusters[cluster_idx].elements[el_idx];
                if (peaks[peak_idx] - el != 1 && peaks[peak_idx] - el != -1)
                {
                    continue; /* check the next element in the cluster */
                }
                n_el = clusters[cluster_idx].n_elements;
                clusters[cluster_idx].elements[n_el] = peaks[peak_idx];
                clusters[cluster_idx].n_elements += 1;
                peak_assigned = true;
                break; /* stop checking elements of the cluster */
            }
            if (peak_assigned)
            {
                break; /* stop checking other clusters */
            }
        }
        if (!peak_assigned)
        {
            /* Peak was not assigned to any cluster => initialize a new cluster. */
            clusters[peak_idx].elements[0] = peaks[peak_idx];
            clusters[peak_idx].n_elements = 1;
        }
    }
}

uint16_t suggest_hand_detections(
    const ifx_f32_t *masked_mean_abs_rdi, uint16_t n_peaks, detection *detections,
    const frame_cfg *f_cfg, const region *search_region,
    const peak_cluster *clusters, float threshold, float bg_level
)
{
    uint16_t n_detections = 0;
    uint16_t search_width = search_region->col_end - search_region->col_start;
    uint16_t doppler_bin = -1, range_bin = -1;
    uint16_t search_start;
    ifx_f32_t search_res_val;
    uint32_t search_res_idx;
    for (int i = 0; i < n_peaks; ++i)
    {
        if (clusters[i].n_elements == 0)
        {
            continue;
        }
        /* Transform doppler_bin from local (ROI) to global (frame) */
        doppler_bin = clusters[i].elements[0] + search_region->row_start;
        search_start = (search_region->row_start + clusters[i].elements[0]) *
                       f_cfg->n_range_bins +
                       search_region->col_start;

        arm_max_f32(
            masked_mean_abs_rdi + search_start, search_width, &search_res_val,
            &search_res_idx
        );
        range_bin = (uint16_t)search_res_idx + search_region->col_start;
        if (search_res_val > threshold * bg_level)
        {
            detections[n_detections].range_bin = range_bin;
            detections[n_detections].doppler_bin = doppler_bin;
            detections[n_detections].value =
                masked_mean_abs_rdi[doppler_bin * f_cfg->n_range_bins + range_bin];
            n_detections += 1;
        }
    }
    return n_detections;
}

const detection *
pick_closest_detection(const detection *detections, uint16_t n_detections)
{
    const detection *res = detections;
    for (int i = 1; i < n_detections; ++i)
    {
        if (detections[i].range_bin < res->range_bin)
        {
            res = detections + i;
        }
    }
    return res;
}

const detection *pick_fastest_detection(
    const detection *detections, uint16_t n_detections, const frame_cfg *f_cfg
) {
    const detection *res = detections;
    uint16_t mid = f_cfg->n_chirps / 2;
    for (int i = 1; i < n_detections; ++i)
    {
        if (abs(detections[i].doppler_bin - mid) > abs(res->doppler_bin - mid))
        {
            res = detections + i;
        }
    }
    return res;
}

const detection *pick_best_hand_detection(
    const detection *detections, uint16_t n_detections, const frame_cfg *f_cfg,
    detection_mode mode
) {
    if (n_detections == 0)
    {
        return NULL;
    }
    switch (mode)
    {
    case DETECTION_MODE_CLOSEST:
        return pick_closest_detection(detections, n_detections);
    case DETECTION_MODE_FASTEST:
        return pick_fastest_detection(detections, n_detections, f_cfg);
    default:
        assert("Unkown detection mode");
    }
    return NULL;
}

detection detect_hand(
    const ifx_f32_t *masked_mean_abs_rdi, const region *search_region,
    const frame_cfg *f_cfg, float bg_level, detection_mode det_mode,
    float threshold
)
{
    uint16_t n_elements = search_region->row_end - search_region->row_start;
    ifx_f32_t* profile = (ifx_f32_t*)malloc(sizeof(ifx_f32_t) * n_elements);
    uint16_t n_peaks = (uint16_t)(0.2 * n_elements);
    uint16_t* peaks = (uint16_t*)malloc(sizeof(uint16_t) * n_peaks);
    uint16_t* cluster_elements = (uint16_t*)malloc(sizeof(uint16_t) * (n_peaks* n_peaks));
    peak_cluster *clusters = (peak_cluster*)malloc(sizeof(peak_cluster) * n_peaks);
    for (int i = 0; i < n_peaks; ++i) {
        clusters[i].elements = cluster_elements + i * n_peaks;
    }
    detection* detections = (detection*)malloc(sizeof(detection) * n_peaks);
    make_doppler_profile(masked_mean_abs_rdi, profile, search_region, f_cfg);
    find_peaks(profile, peaks, n_elements, n_peaks);
    cluster_peaks(peaks, clusters, n_peaks);
    uint16_t n_detections = suggest_hand_detections(
                                masked_mean_abs_rdi, n_peaks, detections, f_cfg, search_region, clusters,
                                threshold, bg_level
                            );
    detection ret_d;
    if (n_detections == 0)
    {
        ret_d = (detection)
        {
            .doppler_bin = -1, .range_bin = -1, .value = -1
        };
    }
    else
    {
        ret_d = *pick_best_hand_detection(detections, n_detections, f_cfg, det_mode);
    }

    free(profile);
    free(peaks);
    free(cluster_elements);
    free(clusters);
    free(detections);

    return ret_d;
 
}

ifx_status angle(ifx_f32_t re, ifx_f32_t im, float *out)
{
    float32_t tmp_out;
    arm_status status;
    status = arm_atan2_f32((float32_t)im, (float32_t)re, &tmp_out);
    *out = (float)tmp_out;
    return (ifx_status)status;
}

float deg2rad(float deg)
{
    return deg * PI / 180.0;
}

float rad2deg(float rad)
{
    return rad / PI * 180.0;
}

float get_phase_difference(float phase0, float phase1)
{
    /* unwrap */
    float d_phase = phase1 - phase0;
    float n_periods = rintf(d_phase / (2 * PI));
    phase1 -= n_periods * 2 * PI;
    d_phase = phase1 - phase0;
    return d_phase;
}

float phase_monopulse(float phase0, float phase1)
{

    float d_phase = get_phase_difference(phase0, phase1);
    return asinf(C0 * d_phase / (2 * PI * FREQ_CENTER * ANTENNA_DISTANCE));
}

void remove_mean_cf64(cfloat32_t *src, uint16_t n_el, uint16_t step_size)
{
    cfloat32_t sum = 0.0f;
    for (int i = 0; i < n_el; ++i)
    {
        sum += src[i * step_size];
    }
    for (int i = 0; i < n_el; ++i)
    {
        src[i * step_size] -= sum / n_el;
    }
}

void remove_mean_3d_cf64(
    ifx_cf64_t *src, uint16_t axis, uint16_t n_ch, uint16_t n_rows,
    uint16_t n_cols
)
{
    cfloat32_t *arr = (cfloat32_t *)src;
    if (axis == 0)
    {
        for (int el = 0; el < n_rows * n_cols; ++el)
        {
            remove_mean_cf64(arr + el, n_ch, n_rows * n_cols);
        }
    } else if (axis == 1)
    {
        for (int ch = 0; ch < n_ch; ++ch)
        {
            for (int col = 0; col < n_cols; ++col)
            {
                remove_mean_cf64(arr + ch * n_rows * n_cols + col, n_rows, n_cols);
            }
        }
    } else if (axis == 2)
    {
        for (int ch = 0; ch < n_ch; ++ch)
        {
            for (int row = 0; row < n_rows; ++row)
            {
                remove_mean_cf64(arr + ch * n_rows * n_cols + row * n_cols, n_cols, 1);
            }
        }
    } else
    {
        abort();
    }
}

void algo(
    algo_output *out, ifx_f32_t *frame, frame_cfg *f_cfg,
    estimate_human_cfg *h_cfg, uint16_t band_min, uint16_t band_max,
    uint16_t band_offset, uint16_t range_min, uint16_t guard_range,
    uint16_t guard_doppler, detection_mode det_mode, float threshold
)
{
    uint16_t rdi_size = f_cfg->n_channels * f_cfg->n_chirps * f_cfg->n_range_bins;
    uint16_t mean_rdi_size = f_cfg->n_chirps * f_cfg->n_range_bins;
    ifx_cf64_t *rdi = (ifx_cf64_t*)calloc(rdi_size, sizeof(ifx_cf64_t));
    ifx_f32_t *abs_rdi = (ifx_f32_t*)malloc(sizeof(ifx_f32_t) * rdi_size);
    ifx_f32_t *mean_abs_rdi = (ifx_f32_t*)malloc(sizeof(ifx_f32_t) * mean_rdi_size);
    ifx_f32_t *masked_mean_abs_rdi = (ifx_f32_t*)malloc(sizeof(ifx_f32_t) * mean_rdi_size);

    build_complex_rdi(frame, rdi, f_cfg);
    arm_cmplx_mag_f32((float32_t *)rdi, (float32_t *)abs_rdi, rdi_size);
    mean_rdi_channel_f32(abs_rdi, mean_abs_rdi, f_cfg);
    estimate_human(mean_abs_rdi, f_cfg, h_cfg);
    uint16_t upper_limit = calculate_upper_range_limit(
                               h_cfg->position_current, band_min, band_offset, range_min
                           );
    uint16_t lower_limit =
        calculate_lower_range_limit(upper_limit, band_max, range_min);
    region hand_search;
    region human_mask;
    get_hand_roi(
        &hand_search, &human_mask, f_cfg, lower_limit, upper_limit, guard_range,
        guard_doppler
    );
    mask_hand_roi(
        mean_abs_rdi, masked_mean_abs_rdi, f_cfg, &hand_search, &human_mask
    );
    float bg_level = get_background_level(masked_mean_abs_rdi, f_cfg);
    detection hand = detect_hand(
                         masked_mean_abs_rdi, &hand_search, f_cfg, bg_level, det_mode, threshold
                     );
    if (hand.range_bin >= f_cfg->n_range_bins)
    {
        out->success = false;
        free(rdi);
        free(abs_rdi);
        free(mean_abs_rdi);
        free(masked_mean_abs_rdi);
        return;
    }

    uint16_t bin_idx[3];
    float phases[3];
    for (int i = 0; i < 3; ++i)
    {
        bin_idx[i] = i * mean_rdi_size + hand.doppler_bin * f_cfg->n_range_bins +
                     hand.range_bin;

        if (angle(rdi[bin_idx[i]].data[0], rdi[bin_idx[i]].data[1], phases + i) != ARM_MATH_SUCCESS)
        {
            out->success = false;
            free(rdi);
            free(abs_rdi);
            free(mean_abs_rdi);
            free(masked_mean_abs_rdi);
            return;
        }
    }

    float azimuth = phase_monopulse(phases[2], phases[0]);
    float elevation = phase_monopulse(phases[2], phases[1]);
    /* Phase correction based on Signify measurements */
    azimuth = azimuth + deg2rad(8.0);
    elevation = elevation + deg2rad(24.0);

    out->success = true;
    out->human_position = h_cfg->position_current;
    out->hand_features = (hand_features
    ) {
        .detection = hand,
        .azimuth = azimuth,
        .elevation = elevation,
        .bg_level = bg_level
    };
    out->lower_limit = lower_limit;
    out->upper_limit = upper_limit;

    free(rdi);
    free(abs_rdi);
    free(mean_abs_rdi);
    free(masked_mean_abs_rdi);

}