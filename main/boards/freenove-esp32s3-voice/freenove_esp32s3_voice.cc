#include "wifi_board.h"
#include "audio/codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "assets/lang_config.h"
#include <esp_random.h>
#include <cstring>
#include <string>
#include <atomic>
#include <algorithm>
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/gpio_led.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_gc9a01.h>

#define TAG "FreenoveEsp32S3Voice"


// Level of the voice currently coming out of the speaker, 0 to 100.
//
// Written by the codec, read by the face. It is needed because the firmware
// reports "speaking" as soon as the server announces the reply, which is before
// the first audio sample arrives: the mouth used to start moving during the
// silence before it. Measuring the real audio makes the mouth move when sound
// actually comes out, and move WITH the voice instead of at a fixed rate.
static std::atomic<int> g_voice_level{0};

// OutputData is virtual and this board is what builds the codec, so the hook
// fits here without patching anything in audio_service.
class LevelMeteringCodec : public NoAudioCodecSimplex {
public:
    using NoAudioCodecSimplex::NoAudioCodecSimplex;

    virtual void OutputData(std::vector<int16_t>& data) override {
        if (!data.empty()) {
            // Mean of the absolute value, sampling one in eight: same result, and eight
            // times cheaper in a loop that runs hundreds of times per second.
            int64_t sum = 0;
            size_t n = 0;
            for (size_t i = 0; i < data.size(); i += 8) {
                sum += std::abs(static_cast<int>(data[i]));
                ++n;
            }
            int mean = n ? static_cast<int>(sum / n) : 0;
            // 6000 out of 32767 is the neighbourhood of ordinary speech; above that the
            // mouth is already fully open.
            g_voice_level.store(std::min(100, mean * 100 / 6000),
                              std::memory_order_relaxed);
        }
        NoAudioCodecSimplex::OutputData(data);
    }
};

// Sofia's face: eyes and mouth, DRAWN.
//
// They are not built from LVGL primitives: they are the frames in face/frames/,
// packed as PNG bytes by face/export_esp32.py. The firmware ships the lodepng
// decoder, so all that happens here is picking one and showing it. Five attempts
// at a procedural eye taught the lesson that an anime eye is drawn, not
// approximated with ovals.
//
// What is still computed here: which mouth frame is due — lip sync follows the
// real audio level — plus blinking, the closed eyes outside a session, the gaze,
// and the switch to the green variant while listening.
//
// Mind the order: anything touching LVGL objects goes AFTER the parent's
// SetupUI(); from the constructor it writes through null pointers and the board
// drops into a boot loop.
#include "eyes_png.h"
#include "mouth_png.h"

class DrawnFace : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    virtual void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);

        auto* screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        // Cover the stock interface. Hidden rather than deleted, because the base
        // class keeps writing to it.
        for (lv_obj_t* o : {container_, content_, emoji_box_, emoji_label_,
                            emoji_image_, top_bar_, status_bar_, preview_image_}) {
            if (o != nullptr) {
                lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            }
        }

        face_ = lv_obj_create(screen);
        lv_obj_remove_style_all(face_);
        lv_obj_set_size(face_, LV_HOR_RES, LV_VER_RES);
        lv_obj_center(face_);
        lv_obj_set_style_bg_color(face_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(face_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);

        // The descriptors wrap the PNG bytes as they are; the decoder works out the
        // format on its own (same pattern as LvglRawImage in
        // display/lvgl_display/lvgl_image.cc).
        MakeDescriptor(open_[0][CENTER], eye_normal_png, eye_normal_png_len);
        MakeDescriptor(open_[0][LEFT], eye_normal_left_png, eye_normal_left_png_len);
        MakeDescriptor(open_[0][RIGHT], eye_normal_right_png, eye_normal_right_png_len);
        MakeDescriptor(open_[0][DOWN], eye_normal_down_png, eye_normal_down_png_len);
        MakeDescriptor(open_[1][CENTER], eye_listening_png, eye_listening_png_len);
        MakeDescriptor(open_[1][LEFT], eye_listening_left_png, eye_listening_left_png_len);
        MakeDescriptor(open_[1][RIGHT], eye_listening_right_png, eye_listening_right_png_len);
        MakeDescriptor(open_[1][DOWN], eye_listening_down_png, eye_listening_down_png_len);
        MakeDescriptor(half_[0], eye_normal_half_png, eye_normal_half_png_len);
        MakeDescriptor(half_[1], eye_listening_half_png, eye_listening_half_png_len);
        MakeDescriptor(closed_[0], eye_normal_closed_png, eye_normal_closed_png_len);
        MakeDescriptor(closed_[1], eye_listening_closed_png, eye_listening_closed_png_len);

        eyes_ = lv_image_create(face_);
        lv_obj_center(eyes_);
        // Starts asleep: being out of session is the natural state at power-on.
        ShowFrame(CLOSED);

        // The mouth: a SECOND image, on top of the eyes. No longer LVGL primitives — a
        // squashed ellipse for the line and a CONSTANT-WIDTH lv_arc for the smile, both
        // in cyan — but the layered drawing from face/frames/, in the same white ink as
        // the eyes.
        //
        // It lives in its own image rather than composited with the eyes, because
        // otherwise every eye gesture would have to be multiplied by every mouth and
        // every opening frame. And it travels CROPPED to its window (104x45 instead of
        // 240x240), so it is positioned rather than centred.
        for (int i = 0; i < MOUTH_TALK_N; ++i) {
            MakeDescriptor(talk_[i], mouth_talk_png[i], mouth_talk_len[i]);
        }
        for (int i = 0; i < MOUTH_GESTURE_N; ++i) {
            MakeDescriptor(gestures_[i], mouth_gesture_png[i], mouth_gesture_len[i]);
        }
        mouth_ = lv_image_create(face_);
        lv_obj_set_pos(mouth_, MOUTH_X, MOUTH_Y);

        SetEmotion("neutral");

        blink_timer_ = lv_timer_create(
            [](lv_timer_t* t) {
                static_cast<DrawnFace*>(lv_timer_get_user_data(t))->Blink();
            }, NextBlink(), this);
        // The gaze: every so often it glances aside and comes back.
        gaze_timer_ = lv_timer_create(
            [](lv_timer_t* t) {
                static_cast<DrawnFace*>(lv_timer_get_user_data(t))->GlanceAside();
            }, NextGaze(), this);
        // The one that walks the frames. Starts paused: each blink wakes it, and it
        // goes back to sleep when the sequence ends.
        seq_timer_ = lv_timer_create(
            [](lv_timer_t* t) {
                auto* self = static_cast<DrawnFace*>(lv_timer_get_user_data(t));
                // One timer walks both sequences: the blink while awake, and the closing or
                // opening of the eyes when the session state changes.
                if (self->eyelids_moving_) self->StepEyelids();
                else                            self->StepBlink();
            }, 40, this);
        lv_timer_pause(seq_timer_);
        // 50 ms: twenty times a second is enough for the mouth to follow the voice
        // without the steps showing.
        lips_timer_ = lv_timer_create(
            [](lv_timer_t* t) {
                static_cast<DrawnFace*>(lv_timer_get_user_data(t))->FollowVoice();
            }, 50, this);
    }

    // The server sends an emotion with every reply. The drawn eyes have only one
    // approved gesture so far (Normal), so the emotion shows in the mouth. Once
    // more eye gestures are approved, they get exported and chosen here.
    virtual void SetEmotion(const char* emotion) override {
        if (emotion == nullptr) return;
        DisplayLockGuard lock(this);
        if (mouth_ == nullptr) return;
        // The emotion -> drawing table is generated by face/export_esp32.py from its
        // GESTURES dict, so the mapping lives in one place and there is no copy here
        // to drift when gestures are added.
        //
        // What no longer exists is per-emotion colour: the mouth is white ink like the
        // eyes, and the only colour left in the face is the iris.
        gesture_ = 0;
        for (size_t i = 0; i < mouth_emotions_n; ++i) {
            if (strcmp(emotion, mouth_emotions[i].emotion) == 0) {
                gesture_ = mouth_emotions[i].gesture;
                break;
            }
        }
        if (!speaking_) ShowMouth();
    }

    virtual void SetStatus(const char* status) override {
        SpiLcdDisplay::SetStatus(status);
        if (status == nullptr) return;
        DisplayLockGuard lock(this);
        if (mouth_ == nullptr) return;

        // Green while listening: the whole drawing switches to the green-iris variant,
        // the sign that the microphone is open.
        //
        // Outside a session Sofia closes her eyes, and opens them when the
        // conversation starts. The rule is defined by what IS a session — connecting,
        // listening or speaking — and not by idleness. Comparing against STANDBY alone
        // left the eyes open: there are more out-of-session states (checking version,
        // loading protocol, activation) and none of them sends that notice, so they
        // never closed.
        bool in_session = (strcmp(status, Lang::Strings::LISTENING) == 0) ||
                         (strcmp(status, Lang::Strings::SPEAKING) == 0) ||
                         (strcmp(status, Lang::Strings::CONNECTING) == 0);
        bool asleep = !in_session;
        if (asleep != asleep_) {
            asleep_ = asleep;
            eyelids_moving_ = true;
            step_ = 0;
            StepEyelids();
        }

        bool listening = (strcmp(status, Lang::Strings::LISTENING) == 0);
        if (listening != listening_) {
            listening_ = listening;
            ShowFrame(frame_);   // same frame, different colour
        }

        bool speaking = (strcmp(status, Lang::Strings::SPEAKING) == 0);
        if (speaking == speaking_) return;
        speaking_ = speaking;
        // The mouth starts CLOSED when speech begins: the audio opens it when sound
        // really comes out, not the notice that it is about to.
        smooth_level_ = 0;
        ShowMouth();
    }

private:
    lv_obj_t* face_ = nullptr;
    lv_obj_t* eyes_ = nullptr;

    lv_obj_t* mouth_ = nullptr;
    lv_image_dsc_t talk_[MOUTH_TALK_N]{};
    lv_image_dsc_t gestures_[MOUTH_GESTURE_N]{};
    int gesture_ = 0;
    lv_timer_t* blink_timer_ = nullptr;
    lv_timer_t* lips_timer_ = nullptr;
    enum { OPEN = 0, HALF = 1, CLOSED = 2 };
    enum { CENTER = 0, LEFT = 1, RIGHT = 2, DOWN = 3 };
    lv_image_dsc_t open_[2][4]{};
    lv_image_dsc_t half_[2]{};
    lv_image_dsc_t closed_[2]{};
    lv_timer_t* gaze_timer_ = nullptr;
    int gaze_ = CENTER;
    lv_timer_t* seq_timer_ = nullptr;
    int step_ = 4;
    int frame_ = OPEN;

    bool speaking_ = false;
    bool listening_ = false;
    bool asleep_ = true;
    bool eyelids_moving_ = false;
    int smooth_level_ = 0;

    static void MakeDescriptor(lv_image_dsc_t& d, const uint8_t* png, size_t n) {
        lv_memzero(&d, sizeof(d));
        d.data = png;
        d.data_size = n;
        d.header.magic = LV_IMAGE_HEADER_MAGIC;
        d.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    }

    // Between 5 and 14 s. It used to be 2.5 to 6, and it blinked far too often.
    static uint32_t NextBlink() { return 5000 + (esp_random() % 9000); }
    // Between 2.5 and 9 s: the gaze moves more often than the blink.
    static uint32_t NextGaze()   { return 2500 + (esp_random() % 6500); }

    // Which mouth drawing is due: while speaking the audio level picks an opening
    // frame; while quiet, the gesture for the emotion.
    void ShowMouth() {
        if (mouth_ == nullptr) return;
        if (speaking_) {
            int i = smooth_level_ * (MOUTH_TALK_N - 1) / 100;
            if (i < 0) i = 0;
            if (i >= MOUTH_TALK_N) i = MOUTH_TALK_N - 1;
            lv_image_set_src(mouth_, &talk_[i]);
        } else {
            lv_image_set_src(mouth_, &gestures_[gesture_]);
        }
    }

    void ShowFrame(int f) {
        frame_ = f;
        const int c = listening_ ? 1 : 0;
        const lv_image_dsc_t* d = (f == HALF)   ? &half_[c]
                                : (f == CLOSED) ? &closed_[c]
                                                 : &open_[c][gaze_];
        lv_image_set_src(eyes_, d);
    }

    // Neither the blink nor the gaze runs on a clock: both intervals are drawn
    // again on every firing. With a fixed period the face beats like a metronome
    // and it reads as a machine immediately.
    void GlanceAside() {
        if (eyes_ == nullptr || asleep_) return;
        // Two glances in three come back to the centre, which is where it really
        // spends most of its time.
        static const uint8_t targets[] = {CENTER, CENTER, LEFT, RIGHT, DOWN};
        uint8_t next = targets[esp_random() % sizeof(targets)];
        if (next != gaze_) {
            gaze_ = next;
            if (frame_ == OPEN) ShowFrame(OPEN);
        }
        lv_timer_set_period(gaze_timer_, NextGaze());
    }

    // The sequence: open, HALF, closed, HALF again, open. The half-open frame
    // appears twice — closing and opening — and that is what tells a blink apart
    // from a cut. Short durations: a whole blink is about 125 ms.
    void Blink() {
        // No blinking with the eyes closed. The timer keeps running rather than being
        // stopped and started: it is cheaper than tracking which timer is alive.
        if (eyes_ == nullptr || asleep_) return;
        step_ = 0;
        StepBlink();
        lv_timer_set_period(blink_timer_, NextBlink());
    }

    // Closing and opening the eyes when entering and leaving a session. It passes
    // through the half-open frame like a blink, but slower: a blink is 125 ms and
    // this is a gesture, not a tic.
    void StepEyelids() {
        static const uint8_t closing[] = {HALF, CLOSED};
        static const uint8_t opening[] = {HALF, OPEN};
        static const uint32_t wait[] = {90, 0};
        if (step_ >= 2) {
            lv_timer_pause(seq_timer_);
            eyelids_moving_ = false;
            return;
        }
        ShowFrame(asleep_ ? closing[step_] : opening[step_]);
        uint32_t ms = wait[step_];
        ++step_;
        if (ms == 0) {
            lv_timer_pause(seq_timer_);
            eyelids_moving_ = false;
            return;
        }
        lv_timer_set_period(seq_timer_, ms);
        lv_timer_reset(seq_timer_);
        lv_timer_resume(seq_timer_);
    }

    void StepBlink() {
        static const uint8_t frames[] = {HALF, CLOSED, HALF, OPEN};
        static const uint32_t wait[] = {35, 55, 35, 0};
        if (step_ >= 4) {
            lv_timer_pause(seq_timer_);
            return;
        }
        ShowFrame(frames[step_]);
        uint32_t ms = wait[step_];
        ++step_;
        if (ms == 0) {
            lv_timer_pause(seq_timer_);
            return;
        }
        lv_timer_set_period(seq_timer_, ms);
        lv_timer_reset(seq_timer_);
        lv_timer_resume(seq_timer_);
    }

    // Follows the level of the audio playing right now. Smoothed, because the raw
    // value jumps a lot between chunks and the mouth would shake.
    void FollowVoice() {
        if (mouth_ == nullptr || !speaking_) return;
        int level = g_voice_level.load(std::memory_order_relaxed);
        smooth_level_ = (smooth_level_ * 6 + level * 4) / 10;
        ShowMouth();
    }
};

class FreenoveEsp32S3Voice : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_ = nullptr;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;   // the GC9A01 sends nothing back
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        // 20 MHz instead of the reference board's 40. This is a PRECAUTION,
        // not a measurement: 40 MHz was never actually tried on this wiring,
        // and no stray pixels were ever observed. Do not read it as a finding.
        //
        // It is not free, either. A full 240x240 eye frame is 115 KB, which
        // takes 46 ms at 20 MHz — longer than the 35 ms steps of the blink, so
        // the animation runs slower than it was written to. At 40 MHz it would
        // be 23 ms. Worth measuring on real hardware and raising if it holds.
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        // No vendor_config: the init commands the upstream reference board carries are
        // for a GC9107, not a GC9A01, and there they are assigned AFTER creating the
        // panel, so they never take effect. An ordinary GC9A01 module is fine with the
        // driver's own.
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new DrawnFace(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                     DISPLAY_SWAP_XY);

    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    FreenoveEsp32S3Voice() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    virtual Led* GetLed() override {
        // Active high, same as the previous ESPHome configuration.
        static GpioLed led(BUILTIN_LED_GPIO, 1);
        return &led;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN,
                                          DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual AudioCodec* GetAudioCodec() override {
        // This board's INMP441 has L/R tied to ground, so it speaks on the LEFT
        // channel. With the default slot mask (stereo) the right channel arrives mute
        // and half the audio is silence: it has to be pinned down. Same finding as the
        // `channel: left` in the ESPHome YAML.
        static LevelMeteringCodec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT, I2S_STD_SLOT_BOTH,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN, I2S_STD_SLOT_LEFT);
        return &audio_codec;
    }
};

DECLARE_BOARD(FreenoveEsp32S3Voice);
