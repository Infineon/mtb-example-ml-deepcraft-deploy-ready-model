/******************************************************************************
* File Name:   preprocess.h
*
* Description: This file contains the function prototypes and constants used
*   in preprocess.c.
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

#ifndef IFXGESTURE_PREPROCESS_H_
#define IFXGESTURE_PREPROCESS_H_

#include "ifx_sensor_dsp.h"
#include <stdint.h>

#define ADC_RESOLUTION (12ul)
#define ADC_NORMALIZATION ((1 << ADC_RESOLUTION) - 1)
#define FREQ_CENTER (60000000000.0)
#define ANTENNA_DISTANCE (0.0025)
#define C0 (299792458.0)

typedef int32_t ifx_status;
typedef float ifx_f32_t;


typedef struct {
    ifx_f32_t data[2];
} ifx_cf64_t;
typedef struct {
    uint16_t n_channels;
    uint16_t n_chirps;
    uint16_t n_samples;
    uint16_t n_range_bins;
} frame_cfg;

typedef struct {
    uint16_t n_chirps;
    uint16_t n_samples;
    bool remove_mean;
    ifx_f32_t *window;
} range_transform_cfg;

typedef struct {
    uint16_t n_chirps;
    uint16_t n_samples;
    bool range_remove_mean;
    bool doppler_remove_mean;
    ifx_f32_t *range_window;
    ifx_f32_t *doppler_window;
} range_doppler_transform_cfg;

typedef struct {
    uint16_t position_min;
    ifx_f32_t position_current;
    ifx_f32_t alpha;
} estimate_human_cfg;

typedef struct {
    uint16_t row_start;
    uint16_t row_end;
    uint16_t col_start;
    uint16_t col_end;
} region;

typedef struct {
    uint16_t n_elements;
    uint16_t *elements;
} peak_cluster;

typedef struct {
    uint16_t doppler_bin;
    uint16_t range_bin;
    ifx_f32_t value;
} detection;

typedef enum {
    DETECTION_MODE_CLOSEST,
    DETECTION_MODE_FASTEST
    /* TODO: implement the "STRONGEST" detection mode
    * DETECTION_MODE_STRONGEST */
} detection_mode;

typedef struct {
    detection detection;
    float azimuth;
    float elevation;
    float bg_level;
} hand_features;

typedef struct {
    bool success;
    ifx_f32_t human_position;
    hand_features hand_features;
    uint16_t lower_limit;
    uint16_t upper_limit;
} algo_output;

void slice_2d_row_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t row, uint16_t n_rows,
    uint16_t n_cols
);

void slice_2d_col_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t col, uint16_t n_rows,
    uint16_t n_cols
);

void slice_3d_row_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t row, uint16_t n_ch,
    uint16_t n_rows, uint16_t n_cols
);

void slice_3d_col_cf64(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t col, uint16_t n_ch,
    uint16_t n_rows, uint16_t n_cols
);

void rfft_f32(ifx_f32_t *x, ifx_cf64_t *out, uint16_t n_samples);

void cfft_f32(ifx_cf64_t *x, uint16_t n_samples);

void fftshift_cf64(ifx_cf64_t *in, uint32_t len);

void range_transform(
    ifx_f32_t *frame_raw, ifx_cf64_t *out, range_transform_cfg *cfg
);

void range_doppler_transform(
    ifx_f32_t *frame_raw, ifx_cf64_t *out, range_doppler_transform_cfg *cfg
);

void build_complex_range_image(
    ifx_f32_t *raw_frame, ifx_cf64_t *out, frame_cfg *f_cfg, ifx_f32_t *window
);

void build_complex_rdi(
    ifx_f32_t *raw_frame, ifx_cf64_t *output_rdi, frame_cfg *f_cfg
);

void mean_rdi_channel_f32(
    ifx_f32_t *abs_rdi, ifx_f32_t *mean, frame_cfg *f_cfg
);

ifx_status cmplx_image_transpose(
    ifx_cf64_t *src, ifx_cf64_t *dst, uint16_t num_rows, uint16_t num_cols
);

ifx_status
cmplx_frame_transpose(ifx_cf64_t *src, ifx_cf64_t *dst, frame_cfg *f_cfg);

void estimate_human(
    ifx_f32_t *frame_abs_rdi, frame_cfg *frame_cfg, estimate_human_cfg *cfg
);

uint16_t calculate_lower_range_limit(
    uint16_t roi_upper_limit, uint16_t band_max, uint16_t range_min
);
uint16_t calculate_upper_range_limit(
    ifx_f32_t position, uint16_t band_min, uint16_t band_offset,
    uint16_t range_min
);

void get_hand_roi(
    region *search_region, region *human_mask, frame_cfg *f_cfg,
    uint16_t lower_range_limit, uint16_t upper_range_limit, uint16_t guard_range,
    uint16_t guard_doppler
);

void mask_hand_roi(
    const ifx_f32_t *mean_abs_rdi, ifx_f32_t *masked_out, const frame_cfg *f_cfg,
    const region *search_region, const region *human_mask
);

float get_background_level(
    const ifx_f32_t *masked_mean_abs_rdi, const frame_cfg *f_cfg
);

void make_doppler_profile(
    const ifx_f32_t *mean_abs_rdi, ifx_f32_t *profile,
    const region *search_region, const frame_cfg *f_cfg
);

void find_peaks(
    const ifx_f32_t *in, uint16_t *idx, uint16_t n_elements, uint16_t n_peaks
);

void cluster_peaks(
    const uint16_t *peaks, peak_cluster *clusters, uint16_t n_peaks
);

uint16_t suggest_hand_detections(
    const ifx_f32_t *masked_mean_abs_rdi, uint16_t n_peaks, detection *detections,
    const frame_cfg *f_cfg, const region *search_region,
    const peak_cluster *clusters, float threshold, float bg_level
);

ifx_status angle(ifx_f32_t re, ifx_f32_t im, float *out);

detection detect_hand(
    const ifx_f32_t *masked_mean_abs_rdi, const region *search_region,
    const frame_cfg *f_cfg, float bg_level, detection_mode det_mode,
    float threshold
);

float get_phase_difference(float phase0, float phase1);

float phase_monopulse(float phase0, float phase1);

float deg2rad(float deg);

float rad2deg(float rad);

void remove_mean_3d_cf64(
    ifx_cf64_t *src, uint16_t axis, uint16_t n_ch, uint16_t n_rows,
    uint16_t n_cols
);

void algo(
    algo_output *out, ifx_f32_t *frame, frame_cfg *f_cfg,
    estimate_human_cfg *h_cfg, uint16_t band_min, uint16_t band_max,
    uint16_t band_offset, uint16_t range_min, uint16_t guard_range,
    uint16_t guard_doppler, detection_mode det_mode, float threshold
);

#endif
