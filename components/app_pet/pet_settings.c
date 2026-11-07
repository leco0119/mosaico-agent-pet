// SPDX-License-Identifier: Apache-2.0
#include "pet_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#include "bundle_gsp.h"

typedef enum { HOME, WIFI, PASSWORD, VOLUME, BINDING } page_t;

enum {
    VOLUME_STEP = 5,
    SSID_DISPLAY_GLYPHS = 19,
    PASSWORD_DISPLAY_BYTES = 28,
    BACKSPACE_KEY = 8,
    ENTER_KEY = 13,
};

struct pet_settings {
    esp_gsp_handle_t ui;
    pet_settings_ops_t ops;
    void *ctx;
    esp_gsp_list_t list;
    page_t page;
    page_t binding_return;
    app_wifi_snapshot_t wifi;
    app_voice_snapshot_t voice;
    bool initialized;
    bool active;
    bool showed_binding;
    bool joining;
    bool password_visible;
    bool extra_symbols;
    uint8_t last_audible;
    char target[APP_WIFI_SSID_BYTES];
    char password[APP_WIFI_PASSWORD_BYTES];
};

static esp_gsp_err_t (*const visible[])(esp_gsp_handle_t, bool) = {
    gsp_pet_home_page_set_visible, gsp_pet_wifi_page_set_visible, gsp_pet_password_page_set_visible,
    gsp_pet_volume_page_set_visible, gsp_pet_binding_page_set_visible,
};

/* SSIDs are byte strings. Keep valid UTF-8 and replace invalid bytes for the
 * display only. The original bytes remain in the connection request. */
static void ssid_text(char out[APP_WIFI_SSID_BYTES + 3], const char *ssid)
{
    size_t offset = 0, written = 0;
    const size_t length = strlen(ssid);
    for (unsigned glyphs = 0; offset < length && glyphs < SSID_DISPLAY_GLYPHS; ++glyphs) {
        const unsigned char c = (unsigned char)ssid[offset];
        unsigned bytes = c < 0x80 ? 1 : c >= 0xC2 && c < 0xE0 ? 2 :
                         c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
        bool valid = c >= 0x20 && c != 0x7F && bytes && offset + bytes <= length;
        for (unsigned i = 1; valid && i < bytes; ++i) {
            valid = ((unsigned char)ssid[offset + i] & 0xC0) == 0x80;
        }
        if (valid && bytes > 2) {
            const unsigned char next = (unsigned char)ssid[offset + 1];
            valid = !(c == 0xE0 && next < 0xA0) && !(c == 0xED && next >= 0xA0) &&
                    !(c == 0xF0 && next < 0x90) && !(c == 0xF4 && next >= 0x90);
        }
        if (valid) {
            memcpy(out + written, ssid + offset, bytes);
            written += bytes;
            offset += bytes;
        } else {
            out[written++] = '?';
            ++offset;
        }
    }
    if (offset < length) {
        memcpy(out + written, "…", 3);
        written += 3;
    }
    out[written] = '\0';
}

static void clear_password(pet_settings_handle_t settings)
{
    volatile char *bytes = settings->password;
    for (size_t i = 0; i < sizeof(settings->password); ++i) bytes[i] = 0;
    settings->password_visible = false;
}

static void show_symbols(pet_settings_handle_t settings, bool extra)
{
    settings->extra_symbols = extra;
    gsp_pet_extra_keys_set_visible(settings->ui, extra);
    gsp_pet_password_keyboard_panel_set_visible(settings->ui, !extra);
    gsp_pet_password_more_set_text(settings->ui, extra ? "返回键盘" : "更多符号");
}

static bool voice_busy(pet_settings_handle_t settings)
{
    app_voice_state_t state = settings->voice.state;
    return (state == APP_VOICE_CONNECTING && settings->voice.ready) || state == APP_VOICE_LISTENING ||
           state == APP_VOICE_THINKING || state == APP_VOICE_SPEAKING;
}

static void set_page(pet_settings_handle_t settings, page_t page)
{
    if (settings->page == page) return;
    if (settings->page == PASSWORD) {
        clear_password(settings);
        gsp_pet_password_value_set_text(settings->ui, "");
    }
    visible[settings->page](settings->ui, false);
    settings->page = page;
    visible[page](settings->ui, true);
    esp_gsp_set_swipe_enabled(settings->ui, !settings->active || page == HOME);
    settings->ops.navigation(settings->ctx, settings->active && page != HOME);
}

static gsp_err_t wifi_row(esp_gsp_handle_t ui, esp_gsp_row_t row, uint32_t index, void *ctx)
{
    pet_settings_handle_t settings = ctx;
    /* A queued native row request can outlive navigation to the other scene. */
    if (!settings->active) return GSP_OK;
    if (index >= settings->wifi.count) {
        return esp_gsp_row_text(ui, row, "") == ESP_GSP_OK ? GSP_OK : GSP_ERR_INVALID_ARG;
    }
    const app_wifi_ap_t *ap = &settings->wifi.aps[index];
    char ssid[APP_WIFI_SSID_BYTES + 3];
    char line[78];
    ssid_text(ssid, ap->ssid);
    const char *type = ap->saved ? "已保存" : ap->secured ? "加密" : "开放";
    const char *signal = ap->rssi >= -60 ? "强" : ap->rssi >= -75 ? "中" : "弱";
    snprintf(line, sizeof(line), "%s  ·  %s  %s", ssid, type, signal);
    return esp_gsp_row_text(ui, row, line) == ESP_GSP_OK ? GSP_OK : GSP_ERR_INVALID_ARG;
}

static void render_wifi(pet_settings_handle_t settings)
{
    esp_gsp_handle_t ui = settings->ui;
    const app_wifi_snapshot_t *wifi = &settings->wifi;
    char name[APP_WIFI_SSID_BYTES + 3];
    ssid_text(name, wifi->ssid);
    gsp_pet_wifi_ssid_set_text(ui, name[0] ? name : "未连接");
    const char *status = wifi->online ? "已连接" : wifi->connecting ? "连接中" : "未连接";
    gsp_pet_wifi_status_set_text(ui, status);
    const char *message = "可用网络";
    if (settings->joining) message = "正在连接";
    else if (wifi->result == APP_WIFI_RESULT_AUTH) message = "密码不正确，请重试";
    else if (wifi->result == APP_WIFI_RESULT_UNREACHABLE) message = "连接失败，请重试";
    else if (wifi->result == APP_WIFI_RESULT_SAVE_FAILED) message = "连接成功，保存失败";
    else if (wifi->scanning) message = "正在扫描";
    else if (wifi->scan_failed) message = "扫描失败，请刷新";
    else if (!wifi->count) message = "未找到网络，请刷新";
    gsp_pet_wifi_message_set_text(ui, message);
    const char *voice = "小智语音   未连接";
    if (settings->voice.ready) voice = "小智语音   已就绪";
    else if (settings->voice.state == APP_VOICE_BINDING) voice = "小智语音   待绑定";
    else if (wifi->online) voice = "小智语音   正在连接";
    gsp_pet_voice_entry_set_text(ui, voice);
}

static void render_password(pet_settings_handle_t settings)
{
    char text[PASSWORD_DISPLAY_BYTES + 1];
    size_t length = strlen(settings->password);
    size_t shown = length < PASSWORD_DISPLAY_BYTES ? length : PASSWORD_DISPLAY_BYTES;
    if (settings->password_visible) memcpy(text, settings->password + length - shown, shown);
    else memset(text, '*', shown);
    text[shown] = '\0';
    gsp_pet_password_value_set_text(settings->ui, length ? text : "输入密码");
    gsp_pet_password_visibility_set_text(settings->ui, settings->password_visible ? "隐藏" : "显示");
    gsp_pet_password_submit_set_text(settings->ui, settings->joining ? "连接中" : "连接");
}

static void open_password(pet_settings_handle_t settings)
{
    char ssid[APP_WIFI_SSID_BYTES + 3];
    clear_password(settings);
    ssid_text(ssid, settings->target);
    gsp_pet_password_ssid_set_text(settings->ui, ssid);
    gsp_pet_password_message_set_text(settings->ui, "");
    show_symbols(settings, false);
    render_password(settings);
    set_page(settings, PASSWORD);
}

static void submit(pet_settings_handle_t settings, const char *password)
{
    if (settings->joining || voice_busy(settings)) return;
    settings->joining = settings->ops.connect(settings->ctx, settings->target, password);
    if (settings->page == PASSWORD) {
        gsp_pet_password_message_set_text(settings->ui, settings->joining ? "正在连接" : "连接失败，请重试");
        render_password(settings);
    } else if (settings->joining) {
        render_wifi(settings);
    } else {
        gsp_pet_wifi_message_set_text(settings->ui, "连接失败，请重试");
    }
}

static void connect_password(pet_settings_handle_t settings)
{
    size_t n = strlen(settings->password);
    if (n < 8 || (n == 64 && strspn(settings->password, "0123456789abcdefABCDEF") != n)) {
        gsp_pet_password_message_set_text(settings->ui, "请输入 8–63 位密码或 64 位十六进制");
        return;
    }
    submit(settings, settings->password);
    if (settings->joining) {
        clear_password(settings);
        render_password(settings);
    }
}

static void password_key(pet_settings_handle_t settings, uint32_t key)
{
    if (settings->joining) return;
    size_t n = strlen(settings->password);
    if (key == ENTER_KEY) {
        connect_password(settings);
        return;
    }
    if (key == BACKSPACE_KEY && n) settings->password[n - 1] = '\0';
    else if (key >= ' ' && key <= '~' && n < sizeof(settings->password) - 1) {
        settings->password[n] = (char)key;
        settings->password[n + 1] = '\0';
    }
    gsp_pet_password_message_set_text(settings->ui, "");
    render_password(settings);
}

static void pick_wifi(pet_settings_handle_t settings, uint32_t index)
{
    if (index >= settings->wifi.count || settings->joining) return;
    if (voice_busy(settings)) {
        gsp_pet_wifi_message_set_text(settings->ui, "通话结束后可切换");
        return;
    }
    const app_wifi_ap_t *ap = &settings->wifi.aps[index];
    memcpy(settings->target, ap->ssid, sizeof(settings->target));
    if (ap->saved) submit(settings, NULL);
    else if (ap->secured) open_password(settings);
    else submit(settings, "");
}

static void refresh_wifi(pet_settings_handle_t settings)
{
    if (voice_busy(settings)) {
        gsp_pet_wifi_message_set_text(settings->ui, "通话结束后可扫描");
    } else if (settings->ops.scan(settings->ctx)) {
        gsp_pet_wifi_message_set_text(settings->ui, "正在扫描");
    } else {
        gsp_pet_wifi_message_set_text(settings->ui, "Wi-Fi 无法使用");
    }
}

static void render_volume(pet_settings_handle_t settings, bool update_slider)
{
    char percent[8];
    const unsigned volume = settings->voice.volume;
    snprintf(percent, sizeof(percent), "%u%%", volume);
    gsp_pet_volume_percent_set_text(settings->ui, percent);
    if (update_slider) gsp_pet_volume_slider_set_value(settings->ui, (int)volume);
    gsp_pet_volume_mute_button_set_text(settings->ui, volume ? "静音" : "取消静音");
}

static void change_volume(pet_settings_handle_t settings, int percent, bool persist, bool update_slider)
{
    if (percent < 0) percent = 0;
    if (percent > APP_VOICE_MAX_VOLUME) percent = APP_VOICE_MAX_VOLUME;
    if (percent) settings->last_audible = percent;
    settings->voice.volume = percent;
    settings->ops.volume(settings->ctx, (unsigned)percent, persist);
    render_volume(settings, update_slider);
}

static void render_binding(pet_settings_handle_t settings)
{
    const app_voice_snapshot_t *voice = &settings->voice;
    const char *status = "正在连接小智";
    const char *code = "− − −";
    const char *hint = "正在检查云端";
    const char *detail = "";
    char spaced[APP_VOICE_BIND_CODE_BYTES + 1];
    if (voice->ready) {
        status = "已连接小智";
        code = "已就绪";
        hint = "按住 AI 键和小智说话";
    } else if (!settings->wifi.online) {
        status = "请先联网";
        hint = "连接 Wi-Fi 后获取绑定码";
    } else if (voice->state == APP_VOICE_BINDING) {
        status = "等待账号绑定";
        if (strlen(voice->binding_code) == APP_VOICE_BIND_CODE_BYTES - 1) {
            snprintf(spaced, sizeof(spaced), "%.3s %.3s", voice->binding_code, voice->binding_code + 3);
            code = spaced;
            hint = "在 xiaozhi.me 登录小智账号";
            detail = "添加设备时输入这组六位码";
        }
    }
    gsp_pet_binding_status_set_text(settings->ui, status);
    gsp_pet_binding_code_set_text(settings->ui, code);
    gsp_pet_binding_caption_set_text(settings->ui, voice->ready ? "语音状态" : "六位绑定码");
    gsp_pet_binding_hint_set_text(settings->ui, hint);
    gsp_pet_binding_detail_set_text(settings->ui, detail);
    gsp_pet_binding_button_set_text(settings->ui, voice->ready ? "返回桌宠" : "检查绑定");
}

static void open_binding(pet_settings_handle_t settings, page_t back)
{
    settings->binding_return = back;
    settings->showed_binding = settings->voice.state == APP_VOICE_BINDING;
    render_binding(settings);
    set_page(settings, BINDING);
}

void pet_settings_handle_event(pet_settings_handle_t settings, const esp_gsp_event_t *event)
{
    if (!settings || !event || event->type != ESP_GSP_EVENT_CALL || event->scene_id != 0) return;
    switch (event->action_id) {
    case GSP_PET_ACT_ID_OPEN_WIFI:
        set_page(settings, WIFI);
        render_wifi(settings);
        refresh_wifi(settings);
        break;
    case GSP_PET_ACT_ID_OPEN_VOLUME:
        render_volume(settings, true);
        set_page(settings, VOLUME);
        break;
    case GSP_PET_ACT_ID_SETTINGS_BACK:
        if (settings->page == PASSWORD) {
            set_page(settings, WIFI);
            render_wifi(settings);
        } else if (settings->page == BINDING) {
            set_page(settings, settings->binding_return);
        } else {
            set_page(settings, HOME);
        }
        break;
    case GSP_PET_ACT_ID_WIFI_REFRESH: refresh_wifi(settings); break;
    case GSP_PET_ACT_ID_WIFI_CHOOSE:
        if (settings->page == WIFI && event->list == settings->list) pick_wifi(settings, event->item);
        break;
    case GSP_PET_ACT_ID_WIFI_KEYBOARD_KEY:
    case GSP_PET_ACT_ID_PASSWORD_EXTRA_KEY:
        if (settings->page == PASSWORD) password_key(settings, event->arg);
        break;
    case GSP_PET_ACT_ID_PASSWORD_MORE:
        if (!settings->joining) show_symbols(settings, !settings->extra_symbols);
        break;
    case GSP_PET_ACT_ID_PASSWORD_TOGGLE:
        settings->password_visible = !settings->password_visible;
        render_password(settings);
        break;
    case GSP_PET_ACT_ID_PASSWORD_CONNECT: connect_password(settings); break;
    case GSP_PET_ACT_ID_VOLUME_DOWN:
        change_volume(settings, settings->voice.volume - VOLUME_STEP, true, true);
        break;
    case GSP_PET_ACT_ID_VOLUME_UP:
        change_volume(settings, settings->voice.volume + VOLUME_STEP, true, true);
        break;
    case GSP_PET_ACT_ID_VOLUME_MOVE:
        change_volume(settings, (int32_t)event->arg, false, false);
        break;
    case GSP_PET_ACT_ID_VOLUME_COMMIT:
        change_volume(settings, (int32_t)event->arg, true, false);
        break;
    case GSP_PET_ACT_ID_VOLUME_MUTE:
        change_volume(settings, settings->voice.volume ? 0 : settings->last_audible, true, true);
        break;
    case GSP_PET_ACT_ID_VOLUME_PREVIEW: settings->ops.preview(settings->ctx); break;
    case GSP_PET_ACT_ID_OPEN_BINDING: open_binding(settings, settings->page); break;
    case GSP_PET_ACT_ID_BINDING_CHECK:
        if (settings->voice.ready) set_page(settings, HOME);
        else settings->ops.check_binding(settings->ctx);
        break;
    default: break;
    }
}

esp_gsp_err_t pet_settings_create(esp_gsp_handle_t ui, const pet_settings_ops_t *ops, void *ctx,
                                  pet_settings_handle_t *out)
{
    if (!ui || !ops || !out) return ESP_GSP_ERR_INVALID_ARG;
    *out = NULL;
    pet_settings_handle_t settings = calloc(1, sizeof(*settings));
    if (!settings) return ESP_GSP_ERR_NO_MEM;
    settings->ui = ui;
    settings->ops = *ops;
    settings->ctx = ctx;
    settings->active = true;
    settings->voice.volume = APP_VOICE_DEFAULT_VOLUME;
    settings->last_audible = APP_VOICE_DEFAULT_VOLUME;
    settings->list = esp_gsp_list_bind_component(ui, GSP_PET_OBJ_KEY_WIFI_LIST, wifi_row, settings);
    if (settings->list == ESP_GSP_LIST_NONE) {
        free(settings);
        return ESP_GSP_ERR_NO_MEM;
    }
    *out = settings;
    return ESP_GSP_OK;
}

void pet_settings_update(pet_settings_handle_t settings, const app_wifi_snapshot_t *wifi,
                         const app_voice_snapshot_t *voice)
{
    if (!settings) return;
    if (!settings->active) {
        settings->wifi = *wifi;
        settings->voice = *voice;
        settings->initialized = false;
        return;
    }
    bool wifi_changed = !settings->initialized || wifi->revision != settings->wifi.revision;
    bool voice_changed = !settings->initialized || voice->revision != settings->voice.revision;
    if (!wifi_changed && !voice_changed) return;
    bool was_binding = settings->voice.state == APP_VOICE_BINDING || settings->showed_binding;
    settings->voice = *voice;
    if (voice->volume) settings->last_audible = voice->volume;
    if (wifi_changed) {
        bool list_changed = !settings->initialized || wifi->count != settings->wifi.count ||
                            memcmp(wifi->aps, settings->wifi.aps, wifi->count * sizeof(wifi->aps[0]));
        settings->wifi = *wifi;
        gsp_pet_wifi_online_dot_set_visible(settings->ui, wifi->online);
        if (list_changed) {
            esp_gsp_list_set_total(settings->ui, settings->list, wifi->count);
            esp_gsp_list_refresh(settings->ui, settings->list);
        }
    }
    settings->initialized = true;
    if (settings->joining && wifi->result != APP_WIFI_RESULT_PENDING) {
        settings->joining = false;
        if (wifi->result == APP_WIFI_RESULT_AUTH && settings->page == WIFI) open_password(settings);
        if (settings->page == PASSWORD) {
            if (wifi->result == APP_WIFI_RESULT_CONNECTED || wifi->result == APP_WIFI_RESULT_SAVE_FAILED) {
                set_page(settings, WIFI);
                refresh_wifi(settings);
            } else {
                const char *error = wifi->result == APP_WIFI_RESULT_AUTH ? "密码不正确，请重试" : "连接失败，请重试";
                gsp_pet_password_message_set_text(settings->ui, error);
                render_password(settings);
            }
        }
    }
    bool pending_binding = voice->state == APP_VOICE_BINDING;
    gsp_pet_bind_shortcut_set_visible(settings->ui, pending_binding);
    if (settings->active && pending_binding && !settings->showed_binding && voice->binding_code[0] &&
        (settings->page == HOME || settings->page == WIFI) && !settings->joining) {
        open_binding(settings, settings->page);
    }
    if (settings->page == BINDING && voice->ready && was_binding) set_page(settings, HOME);
    if (!pending_binding && (!was_binding || voice->ready || !wifi->online)) settings->showed_binding = false;
    if (settings->page == WIFI) render_wifi(settings);
    if (settings->page == VOLUME) render_volume(settings, true);
    if (settings->page == BINDING) render_binding(settings);
}

void pet_settings_destroy(pet_settings_handle_t settings)
{
    if (!settings) return;
    clear_password(settings);
    free(settings);
}

void pet_settings_set_active(pet_settings_handle_t settings, bool active)
{
    if (!settings) return;
    settings->active = active;
    esp_gsp_set_swipe_enabled(settings->ui, !active || settings->page == HOME);
    settings->ops.navigation(settings->ctx, active && settings->page != HOME);
    if (active) {
        settings->initialized = false;
        pet_settings_update(settings, &settings->wifi, &settings->voice);
    }
}
