// Sofia's scenes: the face that turns into a clock, a sun, a cloud, the rain, or
// music.
//
// She closes her eyes; the drawn lids hand over to geometric ones, which inflate
// into the pieces of the scene; what the scene needs and the face has not got
// comes out from behind another piece; the numbers roll like an odometer. Going
// back is not the way it came: the pieces return as closed lids, the brows rise
// out of them, and only then does she open her eyes.
//
// Nothing appears out of nothing and nothing vanishes into nothing, and the
// eyeball never changes size (it is a bitmap here: only the pieces that replace
// the lids and the mouth are geometry).
//
// It is a port of the browser prototype that was approved, so the numbers below
// (positions, durations, the phases of each transition) are the prototype's.
// Everything is a function of time, drawn with LVGL primitives at 30 frames a
// second, so a late or skipped tick never drifts.
//
// The numbers on the face are strokes (face_digits.h, drawn by us): the board
// carries one small font and the project takes no third-party fonts.
//
// Music is the one scene that stays: the other scenes go back to the face a few
// seconds after she speaks, but music stays while it plays, also out of a
// session, until the server says it stopped. The lids become the two heads of a
// pair of quavers, the mouth the beam; the stems grow between them, the pair
// rocks on the beat and small notes rise out of the beam.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "lvgl.h"
#include "face_digits.h"

namespace face {

struct Col { float r, g, b; };

static const Col WHITE = {244, 246, 248};
static const Col SUN_COLOR = {255, 196, 61};
static const Col CLOUD = {214, 224, 235};
static const Col RAIN = {150, 168, 190};
static const Col DROP = {127, 182, 255};
// The small notes that rise out of the beam, in turn.
static const Col NOTE_COLORS[4] = {{92, 163, 242}, {52, 205, 90}, {255, 150, 190}, {255, 196, 61}};

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Col Mix(Col a, Col b, float t) { return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t)}; }
inline float Ease(float p) { return p < 0.5f ? 4 * p * p * p : 1 - std::pow(-2 * p + 2, 3) / 2; }
inline float Win(float v, float a, float b) { return std::min(1.0f, std::max(0.0f, (v - a) / (b - a))); }
inline lv_color_t ToLv(Col c) {
    return lv_color_make(static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b));
}
// The shortest way round between two angles.
inline float LerpAng(float a, float b, float t) {
    float d = std::fmod(b - a, 6.2831853f);
    d = std::fmod(d + 9.424778f, 6.2831853f) - 3.1415927f;
    return a + d * t;
}

enum Scene { FACE_SCENE = 0, CLOCK_SCENE, SUN_SCENE, CLOUD_SCENE, RAIN_SCENE, MUSIC_SCENE };

// ---------------------------------------------------------------- the pieces
struct Ell { float x, y, rx, ry; Col c; };                 // the lids, the sun, a puff
struct Rr  { float x, y, rx, ry, len, ang; Col c; };       // the right lid; may stretch into a hand
struct Cap { float x, y, len, ang, w; Col c; };            // the mouth's line: a hand, an underline, a base
struct Cir { float x, y, r; Col c; };                      // lives in the mouth until the cloud needs it
struct Pose { Ell L; Rr R; Cap M; Cir P; };
struct Fx { float rays = 0, ticks = 0, sec = 0, rain = 0, music = 0; };

struct Target {
    Pose pose;
    Fx fx;
    bool beat = false;
    bool has_text = false;
    std::string text;
    float text_y = 0, text_from_y = 0, clip_top = 0, clip_bot = 240, text_h = 17;
};

inline Pose FacePose() {
    return {{57, 146, 27, 3, WHITE}, {183, 146, 27, 3, 0, 0, WHITE},
            {96, 170, 49, 0, 2.5f, WHITE}, {120, 170, 2, WHITE}};
}

struct SceneArgs {
    Scene scene = FACE_SCENE;
    std::string text;           // "24°": the number that comes up under the weather
    int hour = -1, minute = -1, second = -1;
};

inline Target TargetFor(const SceneArgs& a, float seconds_into_day) {
    Target t;
    t.pose = FacePose();
    if (a.scene == CLOCK_SCENE) {
        float s = seconds_into_day;
        float m = std::fmod(s / 60.0f, 60.0f);
        float h = std::fmod(s / 3600.0f, 12.0f);
        float ma = m / 60.0f * 6.2831853f - 1.5707963f;
        float ha = h / 12.0f * 6.2831853f - 1.5707963f;
        t.pose.L = {120, 120, 7, 7, WHITE};
        t.pose.R = {120, 120, 3.5f, 3.5f, 52, ha, WHITE};
        t.pose.M = {120, 120, 80, ma, 2.5f, WHITE};
        t.pose.P = {120, 120, 3, WHITE};
        t.fx.ticks = 1; t.fx.sec = 1; t.beat = true;
        char buf[8];
        int hh = static_cast<int>(s / 3600.0f) % 24, mm = static_cast<int>(s / 60.0f) % 60;
        std::snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
        t.has_text = true; t.text = buf;
        t.text_y = 176; t.text_from_y = 262; t.clip_top = 0; t.clip_bot = 240; t.text_h = 17;
    } else if (a.scene == SUN_SCENE) {
        t.pose.L = {120, 100, 36, 36, SUN_COLOR};
        t.pose.R = {120, 100, 14, 14, 0, 0, SUN_COLOR};
        t.pose.M = {98, 196, 44, 0, 2, WHITE};
        t.pose.P = {120, 100, 10, SUN_COLOR};
        t.fx.rays = 1; t.beat = true;
        t.has_text = !a.text.empty(); t.text = a.text;
        t.text_y = 176; t.text_from_y = 214; t.clip_top = 0; t.clip_bot = 193; t.text_h = 26;
    } else if (a.scene == CLOUD_SCENE || a.scene == RAIN_SCENE) {
        const Col c = a.scene == RAIN_SCENE ? RAIN : CLOUD;
        t.pose.L = {88, 116, 28, 28, c};
        t.pose.R = {154, 112, 32, 32, 0, 0, c};
        t.pose.M = {86, 136, 68, 0, 14, c};
        t.pose.P = {120, 94, 38, c};
        t.fx.rain = a.scene == RAIN_SCENE ? 1.0f : 0.0f; t.beat = true;
        t.has_text = !a.text.empty(); t.text = a.text;
        t.text_y = 184; t.text_from_y = 136; t.clip_top = 151; t.clip_bot = 240; t.text_h = 26;
    } else if (a.scene == MUSIC_SCENE) {
        // A pair of quavers: the lids are the heads, the mouth the beam. The
        // heads stay ellipses (a Disc cannot tilt), which reads as notes anyway.
        t.pose.L = {88, 166, 17, 13, WHITE};
        t.pose.R = {158, 154, 17, 13, 0, 0, WHITE};
        t.pose.M = {103, 84, 71.02f, -0.16991f, 5, WHITE};      // (103,84) to (173,72)
        t.pose.P = {120, 120, 0, WHITE};
        t.fx.music = 1;
    }
    return t;
}

inline Pose MixPose(const Pose& A, const Pose& B, float p) {
    Pose o;
    o.L = {Lerp(A.L.x, B.L.x, p), Lerp(A.L.y, B.L.y, p), Lerp(A.L.rx, B.L.rx, p), Lerp(A.L.ry, B.L.ry, p),
           Mix(A.L.c, B.L.c, p)};
    o.R = {Lerp(A.R.x, B.R.x, p), Lerp(A.R.y, B.R.y, p), Lerp(A.R.rx, B.R.rx, p), Lerp(A.R.ry, B.R.ry, p),
           Lerp(A.R.len, B.R.len, p), LerpAng(A.R.ang, B.R.ang, p), Mix(A.R.c, B.R.c, p)};
    o.M = {Lerp(A.M.x, B.M.x, p), Lerp(A.M.y, B.M.y, p), Lerp(A.M.len, B.M.len, p),
           LerpAng(A.M.ang, B.M.ang, p), Lerp(A.M.w, B.M.w, p), Mix(A.M.c, B.M.c, p)};
    o.P = {Lerp(A.P.x, B.P.x, p), Lerp(A.P.y, B.P.y, p), Lerp(A.P.r, B.P.r, p), Mix(A.P.c, B.P.c, p)};
    return o;
}

// ------------------------------------------------------------ LVGL wrappers
// Every call into LVGL costs: it invalidates areas the display then has to send over
// SPI. A frame of a scene changes few pieces, so each piece remembers what it last
// showed and does nothing when the new frame asks for the same.
inline void Show(lv_obj_t* o, bool on) {
    if (o == nullptr) return;
    const bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (on && hidden) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!on && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
inline uint32_t Packed(Col c) {
    return (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
}

struct Disc {
    lv_obj_t* o = nullptr;
    int lx = -9999, ly = -9999, lw = -1, lh = -1;
    uint32_t lc = 0xFFFFFFFF;
    void Make(lv_obj_t* parent) {
        o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        Show(o, false);
    }
    void Set(float cx, float cy, float rx, float ry, Col c) {
        int w = static_cast<int>(std::lround(2 * rx)), h = static_cast<int>(std::lround(2 * ry));
        if (w < 1 || h < 1) { Show(o, false); return; }
        const int x = static_cast<int>(std::lround(cx - rx)), y = static_cast<int>(std::lround(cy - ry));
        const uint32_t pc = Packed(c);
        if (x == lx && y == ly && w == lw && h == lh && pc == lc) { Show(o, true); return; }
        Show(o, true);
        if (w != lw || h != lh) lv_obj_set_size(o, w, h);
        if (x != lx || y != ly) lv_obj_set_pos(o, x, y);
        if (pc != lc) lv_obj_set_style_bg_color(o, ToLv(c), 0);
        lx = x; ly = y; lw = w; lh = h; lc = pc;
    }
};

// A line with round caps: a hand, a ray, a stroke of a digit, a brow.
struct Line {
    static const int MAXP = 34;
    lv_obj_t* o = nullptr;
    lv_point_precise_t pts[MAXP];
    int n = 0;
    int l[4] = {-9999, -9999, -9999, -9999}, lwid = -1;
    uint32_t lcol = 0xFFFFFFFF;
    bool poly = false;                 // the last thing drawn was a polyline
    void Make(lv_obj_t* parent) {
        o = lv_line_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_set_style_line_rounded(o, true, 0);
        Show(o, false);
    }
    void Hide() { Show(o, false); }
    void Style(float width, Col c) {
        const int w = std::max(1, static_cast<int>(std::lround(width)));
        const uint32_t pc = Packed(c);
        if (w != lwid) { lv_obj_set_style_line_width(o, w, 0); lwid = w; }
        if (pc != lcol) { lv_obj_set_style_line_color(o, ToLv(c), 0); lcol = pc; }
    }
    void Seg(float x1, float y1, float x2, float y2, float width, Col c) {
        const int a = static_cast<int>(std::lround(x1)), b = static_cast<int>(std::lround(y1)),
                  d = static_cast<int>(std::lround(x2)), e = static_cast<int>(std::lround(y2));
        const bool same = !poly && a == l[0] && b == l[1] && d == l[2] && e == l[3] &&
                          std::max(1, static_cast<int>(std::lround(width))) == lwid && Packed(c) == lcol;
        if (same) { Show(o, true); return; }
        pts[0].x = a; pts[0].y = b; pts[1].x = d; pts[1].y = e;
        n = 2; poly = false;
        l[0] = a; l[1] = b; l[2] = d; l[3] = e;
        Style(width, c);
        Show(o, true);
        lv_line_set_points(o, pts, n);
    }
    void Done(float width, Col c) {
        poly = true;
        Style(width, c);
        Show(o, true);
        lv_line_set_points(o, pts, n);
    }
};

// A cell of the odometer: up to two strokes.
struct Cell {
    Line a, b;
    char last_ch = 0;
    int last_x = -9999, last_y = -9999, last_h = -1;
    void Make(lv_obj_t* parent) { a.Make(parent); b.Make(parent); }
    void Hide() { a.Hide(); b.Hide(); last_ch = 0; }
    // ch: a digit, ':' or 'o' (the degree sign). (x, y): top-left; h: the height.
    void Draw(char ch, float x, float y, float h, float width, Col c) {
        const FaceGlyph* g = nullptr;
        for (int i = 0; i < face_glyphs_n; ++i) if (face_glyphs[i].ch == ch) { g = &face_glyphs[i]; break; }
        if (g == nullptr) { Hide(); return; }
        const int ix = static_cast<int>(std::lround(x)), iy = static_cast<int>(std::lround(y)),
                  ih = static_cast<int>(std::lround(h));
        if (ch == last_ch && ix == last_x && iy == last_y && ih == last_h) {
            Show(a.o, true);
            if (g->strokes > 1) Show(b.o, true);
            return;
        }
        last_ch = ch; last_x = ix; last_y = iy; last_h = ih;
        const float ux = h / 1000.0f;       // pixels per hundredth of a unit: the box is 10 units tall
        Line* lines[2] = {&a, &b};
        for (int s = 0; s < 2; ++s) {
            if (s >= g->strokes) { lines[s]->Hide(); continue; }
            const FaceStroke& st = g->s[s];
            lines[s]->n = st.n;
            for (int i = 0; i < st.n; ++i) {
                lines[s]->pts[i].x = x + st.x[i] * ux;
                lines[s]->pts[i].y = y + st.y[i] * ux;
            }
            lines[s]->Done(width, c);
        }
    }
};

// ---------------------------------------------------------------- the engine
struct Hooks {
    std::function<void(int)> eye_frame;         // 0 rest (open), 1 half, 2 closed
    std::function<void(bool)> bitmap;           // show or hide the drawn eyes and mouth
    std::function<void(bool)> suspend;          // pause or resume blinking, gaze and lip sync
    std::function<Col()> iris;                  // the colour of the iris: blue, or green while listening
};

class Scenes {
public:
    enum Phase { IDLE, LEAVING, SWAPPING, STEADY, RETURNING, SLEEPING, WAKING };
    static constexpr float D = 1500.0f;         // ms of a transition

    void Init(lv_obj_t* parent, Hooks hooks) {
        hooks_ = std::move(hooks);
        for (auto& d : drops_) d.line.Make(parent);
        for (auto& n : notes_) { n.head.Make(parent); n.stem.Make(parent); n.flag.Make(parent); }
        stem_[0].Make(parent); stem_[1].Make(parent);
        text_clip_ = lv_obj_create(parent);
        lv_obj_remove_style_all(text_clip_);
        lv_obj_remove_flag(text_clip_, LV_OBJ_FLAG_SCROLLABLE);
        for (auto& c : cells_) c.Make(text_clip_);
        for (auto& r : rays_) r.Make(parent);
        for (auto& t : ticks_) t.Make(parent);
        sec_.Make(parent); sec_tail_.Make(parent);
        brow_[0].Make(parent); brow_[1].Make(parent);
        P_.Make(parent);
        Rhand_.Make(parent);
        R_.Make(parent);
        M_.Make(parent);
        L_.Make(parent);
        HideAll();
    }

    bool Active() const { return phase_ != IDLE; }
    Phase phase() const { return phase_; }
    Scene scene() const { return scene_; }

    // She is asked for a scene. From the face it opens (LEAVING); from another
    // scene it changes in place (SWAPPING); "face" closes it (RETURNING).
    // closed: she starts with her eyes already shut (out of a session).
    void Start(const SceneArgs& a, int64_t now, bool closed = false) {
        start_closed_ = closed;
        args_ = a;
        base_s_ = (a.hour >= 0 && a.minute >= 0)
                      ? a.hour * 3600.0f + a.minute * 60.0f + std::max(0, a.second)
                      : base_s_;
        base_t_ = now;
        if (a.scene == FACE_SCENE) { Return(now); return; }
        if (phase_ == IDLE || phase_ == SLEEPING || phase_ == WAKING) {
            hooks_.suspend(true);
            from_ = Snapshot{FacePose(), Fx{}};
            kind_ = LEAVING;
        } else {
            from_ = cur_;
            kind_ = SWAPPING;
        }
        scene_ = a.scene;
        phase_ = kind_;
        t0_ = now;
        steady_t_ = 0;
        FadeOutText(now);
    }

    void Return(int64_t now) {
        if (phase_ == IDLE || phase_ == RETURNING || phase_ == SLEEPING || phase_ == WAKING) return;
        from_ = cur_;
        scene_ = FACE_SCENE;
        phase_ = RETURNING;
        t0_ = now;
        FadeOutText(now);
    }

    // The voice: she holds a scene while she speaks and a few seconds after.
    void NoteSpeaking(bool speaking, int64_t now) {
        if (speaking_ && !speaking) spoke_until_ = now;
        speaking_ = speaking;
    }

    // One frame. level is the voice, 0..1.
    void Tick(int64_t now, float level) {
        if (phase_ == IDLE) return;
        const float t = static_cast<float>(now - t0_);
        const float p = std::min(1.0f, t / D);
        Target to = TargetFor(args_, SecondsNow(now));
        if (scene_ == FACE_SCENE) { to.pose = FacePose(); to.fx = Fx{}; to.beat = false; to.has_text = false; }

        float bmp = 0, brow = 0, pieces = 1, extras_out = 0, extras_in = 1;
        int frame = 2;
        bool bitmap = false;

        switch (phase_) {
        case LEAVING:
            frame = start_closed_ ? 2 : p < .05f ? 0 : p < .10f ? 1 : 2;
            bmp = 1 - Ease(Win(p, .12f, .22f));
            bitmap = bmp > 0.5f;
            brow = 1 - Ease(Win(p, .20f, .34f));
            pieces = Ease(Win(p, .36f, .82f));
            extras_in = Ease(Win(p, .68f, 1));
            break;
        case RETURNING:
            frame = 2;
            extras_out = Ease(Win(p, 0, .32f));
            pieces = Ease(Win(p, .22f, .72f));
            brow = Ease(Win(p, .72f, .88f));
            bmp = Ease(Win(p, .88f, 1));
            bitmap = bmp > 0.5f;
            break;
        case SWAPPING:
            extras_out = Ease(Win(p, 0, .35f));
            pieces = Ease(Win(p, .2f, .8f));
            extras_in = Ease(Win(p, .62f, 1));
            brow = 0;
            break;
        case STEADY:
            pieces = 1; extras_in = 1; brow = 0;
            break;
        case SLEEPING:
        case WAKING:
            Waking(now);
            return;
        default: return;
        }

        Pose pz = (phase_ == STEADY) ? to.pose : MixPose(from_.pose, to.pose, pieces);

        // The extras: the old scene's go back first, the new one's come out last;
        // one that both share (cloud to rain) just changes in place.
        Fx fx;
        float fnow[5], ffrom[5] = {from_.fx.rays, from_.fx.ticks, from_.fx.sec, from_.fx.rain, from_.fx.music};
        float ftgt[5] = {to.fx.rays, to.fx.ticks, to.fx.sec, to.fx.rain, to.fx.music};
        for (int i = 0; i < 5; ++i) {
            const float out = ffrom[i] * (1 - extras_out), in = ftgt[i] * extras_in;
            fnow[i] = (phase_ == STEADY) ? ftgt[i]
                      : (ffrom[i] > 0 && ftgt[i] > 0) ? Lerp(ffrom[i], ftgt[i], pieces)
                                                      : std::max(out, in);
        }
        fx.rays = fnow[0]; fx.ticks = fnow[1]; fx.sec = fnow[2]; fx.rain = fnow[3]; fx.music = fnow[4];

        // Phase changes.
        if (p >= 1.0f && phase_ != STEADY) {
            if (phase_ == RETURNING) {
                // Home: closed lids, brows up, the drawing takes over; she sleeps a
                // moment, then opens her eyes.
                HideAll();
                hooks_.eye_frame(2);
                hooks_.bitmap(true);
                phase_ = SLEEPING;
                t0_ = now;
                return;
            }
            phase_ = STEADY;
            steady_t_ = now;
        }

        // The eyes.
        hooks_.eye_frame(frame);
        hooks_.bitmap(bitmap || phase_ == IDLE);
        cur_ = Snapshot{pz, fx};
        Draw(now, pz, fx, brow, !bitmap, to, level, extras_in, extras_out);

        // Back to the face by itself: a few seconds after she finished speaking
        // (or after the scene opened if she never said a word), never more than 40 s.
        // Not music: it stays while it plays, and the server says when it stopped.
        if (phase_ == STEADY && scene_ != MUSIC_SCENE) {
            const int64_t quiet_since = std::max(steady_t_, spoke_until_);
            if ((!speaking_ && now - quiet_since > 5500) || now - steady_t_ > 40000) Return(now);
        }
    }

    void Abort() {               // hard stop: the session closed
        HideAll();
        if (phase_ != IDLE) { hooks_.bitmap(true); hooks_.eye_frame(2); hooks_.suspend(false); }
        phase_ = IDLE;
    }

private:
    struct Snapshot { Pose pose; Fx fx; };
    struct Drop { Line line; float x = 0, y = 999, v = 0; };
    struct Note { Disc head; Line stem, flag; float x0 = 0, y = 999, v = 0, ph = 0; Col c = WHITE; };
    struct TextState {
        std::string str;
        bool live = false;
        float show = 0;
        int64_t born = 0;
        bool leaving = false;
        float leave_from = 0;
        int64_t leave_t0 = 0;
        float y = 0, from_y = 0, clip_top = 0, clip_bot = 240, h = 17;
    };

    Hooks hooks_;
    Phase phase_ = IDLE, kind_ = IDLE;
    Scene scene_ = FACE_SCENE;
    SceneArgs args_;
    int64_t t0_ = 0, steady_t_ = 0, spoke_until_ = 0, base_t_ = 0;
    float base_s_ = 12 * 3600.0f;
    bool speaking_ = false, start_closed_ = false;
    Snapshot from_{FacePose(), Fx{}}, cur_{FacePose(), Fx{}};

    Drop drops_[8];
    Note notes_[4];
    Line stem_[2];
    int64_t next_note_ = 0;
    int note_k_ = 0;
    Line rays_[10], ticks_[12], sec_, sec_tail_, brow_[2], Rhand_, M_;
    Disc P_, R_, L_;
    lv_obj_t* text_clip_ = nullptr;
    Cell cells_[6];
    TextState text_;
    int64_t last_tick_ = 0;

    float SecondsNow(int64_t now) const { return base_s_ + static_cast<float>(now - base_t_) / 1000.0f; }

    void HideAll() {
        for (auto& d : drops_) d.line.Hide();
        for (auto& n : notes_) { n.y = 999; n.head.Set(0, 0, 0, 0, WHITE); n.stem.Hide(); n.flag.Hide(); }
        stem_[0].Hide(); stem_[1].Hide();
        for (auto& r : rays_) r.Hide();
        for (auto& t : ticks_) t.Hide();
        sec_.Hide(); sec_tail_.Hide(); Rhand_.Hide(); M_.Hide();
        brow_[0].Hide(); brow_[1].Hide();
        P_.Set(0, 0, 0, 0, WHITE); R_.Set(0, 0, 0, 0, WHITE); L_.Set(0, 0, 0, 0, WHITE);
        for (auto& c : cells_) c.Hide();
        Show(text_clip_, false);
        text_ = TextState{};
    }

    void FadeOutText(int64_t now) {
        if (text_.live || text_.show > 0) {
            text_.live = false;
            text_.leaving = true;
            text_.leave_from = text_.show;
            text_.leave_t0 = now;
        }
    }

    void Waking(int64_t now) {
        const float t = static_cast<float>(now - t0_);
        if (phase_ == SLEEPING) {
            if (t > 700) { phase_ = WAKING; t0_ = now; hooks_.eye_frame(1); }
        } else if (phase_ == WAKING) {
            if (t > 90) {
                phase_ = IDLE;
                hooks_.eye_frame(0);
                hooks_.suspend(false);
            }
        }
    }

    // A brow: from the arc of the lid to the arc of the brow, k from 0 to 1.
    void Brow(int side, float k) {
        static const float LID[2][3][2] = {{{33, 146}, {57, 140}, {81, 146}}, {{207, 146}, {183, 140}, {159, 146}}};
        static const float BR[2][3][2] = {{{16, 100}, {52, 74}, {90, 94}}, {{224, 100}, {188, 74}, {150, 94}}};
        float P[3][2];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 2; ++j) P[i][j] = Lerp(LID[side][i][j], BR[side][i][j], k);
        Line& l = brow_[side];
        const int N = 16;
        l.n = N + 1;
        for (int i = 0; i <= N; ++i) {
            const float u = static_cast<float>(i) / N, v = 1 - u;
            l.pts[i].x = v * v * P[0][0] + 2 * v * u * P[1][0] + u * u * P[2][0];
            l.pts[i].y = v * v * P[0][1] + 2 * v * u * P[1][1] + u * u * P[2][1];
        }
        l.Done(Lerp(3, 4, k), WHITE);
    }

    void Draw(int64_t now, const Pose& pz, const Fx& fx, float brow, bool geo, const Target& to,
              float level, float extras_in, float extras_out) {
        const float dt = last_tick_ ? std::min(0.05f, static_cast<float>(now - last_tick_) / 1000.0f) : 0.033f;
        last_tick_ = now;
        const float beat = to.beat ? level : 0.0f;
        const Col iris = hooks_.iris ? hooks_.iris() : Col{92, 163, 242};

        // Rain: born behind the cloud's base, falling out through the bottom.
        if (fx.rain > 0.01f && (std::rand() % 1000) < static_cast<int>(450 * fx.rain)) {
            for (auto& d : drops_) if (d.y > 260) {
                d.x = 90 + static_cast<float>(std::rand() % 60);
                d.y = 138; d.v = 120 + static_cast<float>(std::rand() % 60);
                break;
            }
        }
        for (auto& d : drops_) {
            if (d.y > 260) { d.line.Hide(); continue; }
            d.y += d.v * dt;
            d.line.Seg(d.x, d.y, d.x - 3, d.y + 10, 3, DROP);
        }

        // Music: small notes rise out of the beam, sway, and leave through the top.
        if (fx.music > 0.6f && now >= next_note_) {
            for (auto& n : notes_) if (n.y > 900) {
                n.x0 = 104 + static_cast<float>(std::rand() % 68);
                n.y = 70; n.v = 26 + static_cast<float>(std::rand() % 10);
                n.ph = static_cast<float>(std::rand() % 628) / 100.0f;
                n.c = NOTE_COLORS[note_k_++ % 4];
                break;
            }
            next_note_ = now + 950;
        }
        for (auto& n : notes_) {
            if (n.y > 900) { n.head.Set(0, 0, 0, 0, WHITE); n.stem.Hide(); n.flag.Hide(); continue; }
            n.y -= n.v * dt;
            if (n.y < 14) { n.y = 999; continue; }
            const float x = n.x0 + std::sin(static_cast<float>(now) / 420.0f + n.ph) * 7;
            n.head.Set(x, n.y, 6, 4.5f, n.c);
            n.stem.Seg(x + 5, n.y - 1, x + 5, n.y - 17, 2.5f, n.c);
            n.flag.Seg(x + 5, n.y - 17, x + 11, n.y - 11, 2.5f, n.c);
        }

        // The text: under every piece, rolling out from behind the one it belongs to.
        UpdateText(now, to, extras_in);

        if (!geo) {
            stem_[0].Hide(); stem_[1].Hide();
            for (auto& r : rays_) r.Hide();
            for (auto& t : ticks_) t.Hide();
            sec_.Hide(); sec_tail_.Hide(); Rhand_.Hide(); M_.Hide();
            P_.Set(0, 0, 0, 0, WHITE); R_.Set(0, 0, 0, 0, WHITE); L_.Set(0, 0, 0, 0, WHITE);
            brow_[0].Hide(); brow_[1].Hide();
            return;
        }

        const Ell& L = pz.L; const Rr& R = pz.R; const Cap& M = pz.M; const Cir& P = pz.P;

        if (fx.music > 0.001f) {
            // The pair rocks on the beat, the heads swell in turn. It knows no
            // tempo (the music plays on another speaker), so the beat is its own.
            const float tt = static_cast<float>(now);
            const float bob = std::sin(tt / 111.0f) * 4 * fx.music;
            const float sl = 1 + 0.07f * fx.music * std::max(0.0f, std::sin(tt / 222.0f));
            const float sr = 1 + 0.07f * fx.music * std::max(0.0f, -std::sin(tt / 222.0f));
            P_.Set(P.x, P.y + bob, P.r, P.r, P.c);
            // The stems grow from the heads up to the beam.
            const float tn = std::fabs(std::cos(M.ang)) > 1e-3f ? std::tan(M.ang) : 0.0f;
            const float hx[2] = {L.x, R.x}, hy[2] = {L.y, R.y}, hr[2] = {L.rx, R.rx};
            for (int i = 0; i < 2; ++i) {
                const float sx = hx[i] + hr[i] - 2, top = M.y + tn * (sx - M.x), y0 = hy[i] - 3;
                stem_[i].Seg(sx, y0 + bob, sx, Lerp(y0, top, fx.music) + bob, 5, WHITE);
            }
            Rhand_.Hide();
            R_.Set(R.x, R.y + bob, R.rx * sr, R.ry * sr, R.c);
            M_.Seg(M.x, M.y + bob, M.x + std::cos(M.ang) * M.len, M.y + std::sin(M.ang) * M.len + bob, 2 * M.w, M.c);
            L_.Set(L.x, L.y + bob, L.rx * sl, L.ry * sl, L.c);
            for (auto& r : rays_) r.Hide();
            for (auto& t : ticks_) t.Hide();
            sec_.Hide(); sec_tail_.Hide();
            if (brow > 0.001f) { Brow(0, brow); Brow(1, brow); }
            else { brow_[0].Hide(); brow_[1].Hide(); }
            return;
        }
        stem_[0].Hide(); stem_[1].Hide();

        if (fx.rays > 0.001f) {
            const float rot = static_cast<float>(now) / 4000.0f, ext = fx.rays * (18 + beat * 10);
            for (int i = 0; i < 10; ++i) {
                const float a = rot + i / 10.0f * 6.2831853f, len = L.rx + 6 + ext;
                rays_[i].Seg(L.x, L.y, L.x + std::cos(a) * len, L.y + std::sin(a) * len, 6, L.c);
            }
        } else for (auto& r : rays_) r.Hide();

        if (fx.ticks > 0.001f) {
            const float off = (1 - fx.ticks) * 40;
            for (int i = 0; i < 12; ++i) {
                const float a = i / 12.0f * 6.2831853f - 1.5707963f;
                const bool big = i % 3 == 0;
                const float r0 = (big ? 93.0f : 99.0f) + off, r1 = 108 + off;
                ticks_[i].Seg(120 + std::cos(a) * r0, 120 + std::sin(a) * r0,
                              120 + std::cos(a) * r1, 120 + std::sin(a) * r1, big ? 5 : 3, WHITE);
            }
        } else for (auto& t : ticks_) t.Hide();

        if (fx.sec > 0.001f) {
            const float s = std::fmod(SecondsNow(now), 60.0f);
            const float a = s / 60.0f * 6.2831853f - 1.5707963f;
            sec_.Seg(120, 120, 120 + std::cos(a) * 90 * fx.sec, 120 + std::sin(a) * 90 * fx.sec, 2, iris);
            sec_tail_.Seg(120, 120, 120 - std::cos(a) * 14 * fx.sec, 120 - std::sin(a) * 14 * fx.sec, 2, iris);
        } else { sec_.Hide(); sec_tail_.Hide(); }

        // The brows fold into the lids, or rise out of them.
        if (brow > 0.001f) { Brow(0, brow); Brow(1, brow); }
        else { brow_[0].Hide(); brow_[1].Hide(); }

        const float swell = 1 + beat * 0.06f;
        P_.Set(P.x, P.y, P.r * swell, P.r * swell, P.c);
        if (R.len > 0.5f) {
            Rhand_.Seg(R.x, R.y, R.x + std::cos(R.ang) * R.len, R.y + std::sin(R.ang) * R.len,
                       2 * std::min(R.rx, R.ry), R.c);
        } else Rhand_.Hide();
        R_.Set(R.x, R.y, R.rx * swell, R.ry * swell, R.c);
        M_.Seg(M.x, M.y, M.x + std::cos(M.ang) * M.len, M.y + std::sin(M.ang) * M.len, 2 * M.w, M.c);
        const float hub = (scene_ == CLOCK_SCENE) ? beat * 4 : 0;
        L_.Set(L.x, L.y, L.rx * swell + hub, L.ry * swell + hub, L.c);
        (void)extras_out;
    }

    void UpdateText(int64_t now, const Target& to, float extras_in) {
        // A new text is born once the extras start to come in.
        if (to.has_text && extras_in > 0 && scene_ != FACE_SCENE && !text_.live && !text_.leaving) {
            text_.str = to.text; text_.live = true; text_.born = now; text_.show = 0;
            text_.y = to.text_y; text_.from_y = to.text_from_y; text_.clip_top = to.clip_top;
            text_.clip_bot = to.clip_bot; text_.h = to.text_h;
        }
        if (text_.live && scene_ == CLOCK_SCENE) text_.str = to.text;       // the minute turns over
        if (text_.leaving) {
            text_.show = text_.leave_from * (1 - Ease(std::min(1.0f, static_cast<float>(now - text_.leave_t0) / (D * .32f))));
            if (text_.show <= 0.001f) { text_.leaving = false; text_.show = 0; }
        } else if (text_.live) {
            text_.show = Ease(std::min(1.0f, static_cast<float>(now - text_.born) / (D * .38f)));
        }
        if (text_.show <= 0.001f || text_.str.empty()) {
            for (auto& c : cells_) c.Hide();
            Show(text_clip_, false);
            return;
        }
        // The window the digits roll inside: they come out from behind a piece.
        Show(text_clip_, true);
        lv_obj_set_pos(text_clip_, 0, static_cast<int>(text_.clip_top));
        lv_obj_set_size(text_clip_, 240, std::max(1, static_cast<int>(text_.clip_bot - text_.clip_top)));
        const float h = text_.h, w = h * 0.6f, stroke = std::max(2.0f, h * 0.11f);
        const float y = Lerp(text_.from_y, text_.y, text_.show) - h / 2 - text_.clip_top;
        // width of the string
        float total = 0;
        for (char ch : text_.str) total += (ch == ':') ? w * 0.55f : w * 1.18f;
        float x = 120 - total / 2;
        int ci = 0;
        for (size_t i = 0; i < text_.str.size() && ci < 6; ++i) {
            char ch = text_.str[i];
            // the degree sign comes from the string as a UTF-8 pair: draw it as 'o'
            if (static_cast<unsigned char>(ch) == 0xC2 && i + 1 < text_.str.size()) { ch = 'o'; ++i; }
            const bool digit = ch >= '0' && ch <= '9';
            char shown = ch;
            if (digit && !text_.leaving) {
                // each digit counts up from 0 to its value, one after another
                const float roll = Ease(Win(static_cast<float>(now - text_.born) - i * 90.0f, 0, 900));
                shown = static_cast<char>('0' + static_cast<int>((ch - '0') * roll));
            }
            const float cw = (ch == ':') ? w * 0.55f : w * 1.18f;
            cells_[ci].Draw(shown, x, y, h, stroke, WHITE);
            x += cw; ++ci;
        }
        for (; ci < 6; ++ci) cells_[ci].Hide();
    }
};

}  // namespace face
