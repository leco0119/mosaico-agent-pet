#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pet_pages.h"

struct pet_settings { unsigned events; bool active; };
static unsigned refreshed;
static bool pet_active = true;
void app_pet_set_active(app_pet_handle_t pet, bool active)
{ (void)pet; pet_active = active; }
void pet_settings_handle_event(pet_settings_handle_t s, const esp_gsp_event_t *event)
{ (void)event; ++s->events; }
void pet_settings_set_active(pet_settings_handle_t s, bool active)
{ s->active = active; }
static void refresh(void *ctx) { (void)ctx; ++refreshed; }

int main(void)
{
    struct fake_ui ui = {0};
    struct pet_settings settings = {.active = true};
    app_usage_handle_t usage = NULL;
    pet_pages_handle_t pages = NULL;
    assert(app_usage_create(&ui, &usage) == ESP_GSP_OK);
    assert(app_usage_on_refresh(usage, refresh, NULL) == ESP_GSP_OK);
    assert(pet_pages_create(&ui, &settings, (app_pet_handle_t)&settings, usage, &pages) == ESP_GSP_OK);
    assert(ui.swipe_enabled);
    esp_gsp_event_t event = {.action_id = 7, .refresh = true, .scene_id = 0};
    ui.event_cb(&ui, &event, ui.event_ctx);
    assert(settings.events == 1 && refreshed == 0);
    event.action_id = 100;
    ui.event_cb(&ui, &event, ui.event_ctx);
    assert(ui.scene == 1 && !settings.active && !pet_active);
    event.scene_id = 1;
    event.action_id = 7;
    ui.event_cb(&ui, &event, ui.event_ctx);
    assert(settings.events == 1 && refreshed == 1);
    ui.event_cb(&ui, &event, ui.event_ctx);
    assert(refreshed == 1); /* coalesce repeated clicks while waiting */
    assert(strstr(ui.action, "更新中"));
    event.action_id = 100;
    ui.event_cb(&ui, &event, ui.event_ctx);
    assert(ui.scene == 0 && settings.active && pet_active);
    assert(pet_pages_destroy(&pages) == ESP_GSP_OK && !ui.event_cb);
    assert(app_usage_destroy(&usage) == ESP_GSP_OK);
    puts("pet_pages: scene-qualified routing, navigation and refresh ok");
    return 0;
}
