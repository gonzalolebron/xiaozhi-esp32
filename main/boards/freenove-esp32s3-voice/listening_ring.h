// The listening ring: a green circle along the edge of the round display while
// the microphone is open.
//
// The green iris says the same, but the iris is 32 pixels wide and from across
// the room it cannot be told from the blue one. The ring is the whole edge of
// the glass.
//
// It fades in when she starts listening and fades out when she stops. The fade
// is short on purpose: the ring spans the whole screen, so every step of it
// repaints the whole screen. Once lit or gone it costs nothing of its own.
//
// tests/test_listening_ring.py reads the constants and checks that the ring
// touches neither the eyes nor the clock, and that its green is the iris's.
#pragma once

#include <cstdint>
#include <cstdlib>

#include "lvgl.h"

namespace face {

// The eyes reach 108 pixels from the centre and the hour marks of the clock 111;
// the ring starts at 114.
constexpr int kListeningRingWidth = 6;
// The green of the drawn iris (face/frames/eye_listening.png).
constexpr uint32_t kListeningRingColor = 0x30D158;
// The whole fade, either way.
constexpr uint32_t kListeningRingFadeMs = 200;

class ListeningRing {
public:
    // Made last, so it lies above the face and the scenes. Starts hidden.
    void Make(lv_obj_t* parent) {
        ring_ = lv_obj_create(parent);
        lv_obj_remove_style_all(ring_);
        lv_obj_set_size(ring_, LV_HOR_RES, LV_VER_RES);
        lv_obj_center(ring_);
        lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring_, kListeningRingWidth, 0);
        lv_obj_set_style_border_color(ring_, lv_color_hex(kListeningRingColor), 0);
        lv_obj_set_style_border_opa(ring_, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(ring_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ring_, LV_OBJ_FLAG_HIDDEN);
    }

    // Fades from wherever it is, so a quick listen-speak-listen does not jump.
    void Show(bool on) {
        if (ring_ == nullptr) return;
        const int32_t from = lv_obj_get_style_border_opa(ring_, LV_PART_MAIN);
        const int32_t to = on ? LV_OPA_COVER : LV_OPA_TRANSP;
        lv_anim_delete(ring_, SetOpa);
        if (from == to) return;
        lv_obj_remove_flag(ring_, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ring_);
        lv_anim_set_exec_cb(&a, SetOpa);
        lv_anim_set_values(&a, from, to);
        lv_anim_set_duration(&a, kListeningRingFadeMs * std::abs(to - from) / LV_OPA_COVER);
        // Hidden once gone, so it is not walked on every redraw of the eyes.
        if (!on) lv_anim_set_completed_cb(&a, [](lv_anim_t* done) {
            lv_obj_add_flag(static_cast<lv_obj_t*>(done->var), LV_OBJ_FLAG_HIDDEN);
        });
        lv_anim_start(&a);
    }

private:
    lv_obj_t* ring_ = nullptr;

    static void SetOpa(void* ring, int32_t opa) {
        lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(ring), static_cast<lv_opa_t>(opa), 0);
    }
};

}  // namespace face
