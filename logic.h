#pragma once

#include <stdint.h>
#include <math.h>

namespace lamp {

inline float ease_in_cubic(float t) { return t * t * t; }

inline float ease_out_cubic(float t) {
    float f = t - 1;
    return f * f * f + 1;
}

inline void kelvin_rgb(uint16_t kelvin, uint8_t *r, uint8_t *g, uint8_t *b) {
    float t = kelvin / 100.0f;
    float rf, gf, bf;

    if (t <= 66) rf = 255;
    else rf = 329.698727446f * powf(t - 60, -0.1332047592f);

    if (t <= 66) gf = 99.4708025861f * logf(t) - 161.1195681661f;
    else gf = 288.1221695283f * powf(t - 60, -0.0755148492f);

    if (t <= 66) bf = (t <= 19) ? 0 : 138.5177312231f * logf(t - 10) - 305.0447927307f;
    else bf = 255;

    const float vals[3] = { rf, gf, bf };
    uint8_t *outs[3] = { r, g, b };
    for (int i = 0; i < 3; i++) {
        float v = vals[i];
        outs[i][0] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
}

struct ScheduleParams {
    uint8_t  bs_h;
    uint8_t  bs_m;
    uint32_t bs_d;
    uint8_t  ws_h;
    uint8_t  ws_m;
    uint32_t ws_d;
    uint8_t  dim_floor;
    uint16_t dim_cct;
    uint8_t  full_bri;
    uint16_t default_cct;
    uint16_t wake_peak_cct;
};

struct LampOutput {
    uint8_t  brightness;
    uint16_t cct;
};

inline LampOutput schedule_output(uint32_t now_s, const ScheduleParams &p) {
    LampOutput out;

    uint32_t ws  = (uint32_t)p.ws_h * 3600 + p.ws_m * 60;
    uint32_t bs  = (uint32_t)p.bs_h * 3600 + p.bs_m * 60;
    uint32_t be  = (bs + p.bs_d) % 86400;
    uint32_t e_ws = (now_s - ws + 86400) % 86400;
    uint32_t e_bs = (now_s - bs + 86400) % 86400;

    if (e_ws < p.ws_d) {
        float f = (float)e_ws / p.ws_d;
        out.brightness = p.dim_floor + (uint8_t)((p.full_bri - p.dim_floor) * ease_out_cubic(f));
        out.cct = p.dim_cct + (uint16_t)((p.wake_peak_cct - p.dim_cct) * f);
    } else if (e_bs < p.bs_d) {
        float f = ease_in_cubic((float)e_bs / p.bs_d);
        out.brightness = p.full_bri - (uint8_t)((p.full_bri - p.dim_floor) * f);
        out.cct = p.default_cct - (uint16_t)((p.default_cct - p.dim_cct) * f);
    } else {
        uint32_t gap  = (ws - be + 86400) % 86400;
        uint32_t e_be = (now_s - be + 86400) % 86400;
        if (gap > 0 && e_be < gap) {
            out.brightness = p.dim_floor;
            out.cct = p.dim_cct;
        } else {
            out.brightness = p.full_bri;
            out.cct = p.default_cct;
        }
    }
    return out;
}

inline bool schedule_window_active(uint32_t now_s, const ScheduleParams &p) {
    uint32_t ws  = (uint32_t)p.ws_h * 3600 + p.ws_m * 60;
    uint32_t be  = ((uint32_t)p.bs_h * 3600 + p.bs_m * 60 + p.bs_d) % 86400;
    uint32_t gap = (ws - be + 86400) % 86400;
    if (gap == 0) return true;
    uint32_t e_be = (now_s - be + 86400) % 86400;
    return e_be >= gap;
}

} // namespace lamp
