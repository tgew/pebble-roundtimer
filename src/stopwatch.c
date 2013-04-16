/*
 * Pebble Stopwatch - the big, ugly file.
 * Copyright (C) 2013 Katharine Berry
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"

#include "laps.h"
#include "config.h"
#include "common.h"

#define MY_UUID { 0xC2, 0x5A, 0x8D, 0x50, 0x10, 0x5F, 0x45, 0xBF, 0xB9, 0x92, 0xCF, 0xF9, 0x58, 0xA7, 0x93, 0xAD }
PBL_APP_INFO(MY_UUID,
             "Round Timer", "Jason Chu",
             1, 0, /* App version */
             RESOURCE_ID_IMAGE_MENU_ICON,
             APP_INFO_STANDARD_APP);

AppContextRef app;

// Main display
static TextLayer big_time_layer;
static TextLayer seconds_time_layer;
static Layer line_layer;
static BmpContainer button_labels;


// Lap time display
#define LAP_TIME_SIZE 5
static char lap_times[LAP_TIME_SIZE][11] = {"00:00:00.0", "00:01:00.0", "00:02:00.0", "00:03:00.0", "00:04:00.0"};
static TextLayer lap_layers[LAP_TIME_SIZE]; // an extra temporary layer
static int next_lap_layer = 0;
static int last_lap_time = 0;

int last_period = -1;

// The documentation claims this is defined, but it is not.
// Define it here for now.
#ifndef APP_TIMER_INVALID_HANDLE
    #define APP_TIMER_INVALID_HANDLE 0xDEADBEEF
#endif

// Actually keeping track of time
static time_t elapsed_time = 0;
static bool started = false;
static AppTimerHandle update_timer = APP_TIMER_INVALID_HANDLE;
// We want hundredths of a second, but Pebble won't give us that.
// Pebble's timers are also too inaccurate (we run fast for some reason)
// Instead, we count our own time but also adjust ourselves every pebble
// clock tick. We maintain our original offset in hundredths of a second
// from the first tick. This should ensure that we always have accurate times.
static time_t start_time = 0;
static time_t last_pebble_time = 0;

// Global animation lock. As long as we only try doing things while
// this is zero, we shouldn't crash the watch.
static int busy_animating = 0;

#define TIMER_UPDATE 1
#define FONT_BIG_TIME RESOURCE_ID_FONT_DEJAVU_SANS_BOLD_SUBSET_30
#define FONT_SECONDS RESOURCE_ID_FONT_DEJAVU_SANS_SUBSET_18
#define FONT_LAPS RESOURCE_ID_FONT_DEJAVU_SANS_SUBSET_22

#define BUTTON_LAP BUTTON_ID_DOWN
#define BUTTON_RUN BUTTON_ID_SELECT
#define BUTTON_RESET BUTTON_ID_UP

void toggle_stopwatch_handler(ClickRecognizerRef recognizer, Window *window);
void main_config_provider(ClickConfig **config, Window *window);
void handle_init(AppContextRef ctx);
time_t time_seconds();
void stop_stopwatch();
void start_stopwatch();
void toggle_stopwatch_handler(ClickRecognizerRef recognizer, Window *window);
void reset_stopwatch_handler(ClickRecognizerRef recognizer, Window *window);
void update_stopwatch();
void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie);
void pbl_main(void *params);
void draw_line(Layer *me, GContext* ctx);
void save_lap_time(int seconds);
void lap_time_handler(ClickRecognizerRef recognizer, Window *window);
void shift_lap_layer(PropertyAnimation* animation, Layer* layer, GRect* target, int distance_multiplier);
time_t single_round_running_time();
int get_round_period();
time_t current_counter();
void period_changed();

void handle_init(AppContextRef ctx) {
    app = ctx;

    // Main window setup
    window_init(&main_window, "Round Timer");
    //window_stack_push(&main_window, true /* Animated */);
    window_set_background_color(&main_window, GColorBlack);
    window_set_fullscreen(&main_window, false);

    resource_init_current_app(&APP_RESOURCES);

    // Arrange for user input.
    window_set_click_config_provider(&main_window, (ClickConfigProvider) main_config_provider);

    // Get our fonts
    GFont big_font = fonts_load_custom_font(resource_get_handle(FONT_BIG_TIME));
    GFont seconds_font = fonts_load_custom_font(resource_get_handle(FONT_SECONDS));
    GFont laps_font = fonts_load_custom_font(resource_get_handle(FONT_LAPS));

    // Root layer
    Layer *root_layer = window_get_root_layer(&main_window);

    // Set up the big timer.
    text_layer_init(&big_time_layer, GRect(0, 5, 96, 35));
    text_layer_set_background_color(&big_time_layer, GColorBlack);
    text_layer_set_font(&big_time_layer, big_font);
    text_layer_set_text_color(&big_time_layer, GColorWhite);
    text_layer_set_text(&big_time_layer, "00:00");
    text_layer_set_text_alignment(&big_time_layer, GTextAlignmentRight);
    layer_add_child(root_layer, &big_time_layer.layer);

    text_layer_init(&seconds_time_layer, GRect(96, 17, 49, 35));
    text_layer_set_background_color(&seconds_time_layer, GColorBlack);
    text_layer_set_font(&seconds_time_layer, seconds_font);
    text_layer_set_text_color(&seconds_time_layer, GColorWhite);
    text_layer_set_text(&seconds_time_layer, ".0");
    layer_add_child(root_layer, &seconds_time_layer.layer);

    // Set up the lap time layers. These will be made visible later.
    for(int i = 0; i < LAP_TIME_SIZE; ++i) {
        text_layer_init(&lap_layers[i], GRect(-139, 52, 139, 30));
        text_layer_set_background_color(&lap_layers[i], GColorClear);
        text_layer_set_font(&lap_layers[i], laps_font);
        text_layer_set_text_color(&lap_layers[i], GColorWhite);
        text_layer_set_text(&lap_layers[i], lap_times[i]);
        layer_add_child(root_layer, &lap_layers[i].layer);
    }

    // Add some button labels
    bmp_init_container(RESOURCE_ID_IMAGE_BUTTON_LABELS, &button_labels);
    layer_set_frame(&button_labels.layer.layer, GRect(130, 10, 14, 136));
    layer_add_child(root_layer, &button_labels.layer.layer);

    init_config_window();
}

void handle_deinit(AppContextRef ctx) {
    bmp_deinit_container(&button_labels);
}

void draw_line(Layer *me, GContext* ctx) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_line(ctx, GPoint(0, 0), GPoint(140, 0));
    graphics_draw_line(ctx, GPoint(0, 1), GPoint(140, 1));
}

void stop_stopwatch() {
    started = false;
    if(update_timer != APP_TIMER_INVALID_HANDLE) {
        if(app_timer_cancel_event(app, update_timer)) {
            update_timer = APP_TIMER_INVALID_HANDLE;
        }
    }
}

void start_stopwatch() {
    started = true;
    last_pebble_time = 0;
    start_time = 0;
    update_timer = app_timer_send_event(app, 100, TIMER_UPDATE);
}

void toggle_stopwatch_handler(ClickRecognizerRef recognizer, Window *window) {
    if(started) {
        stop_stopwatch();
    } else {
        start_stopwatch();
    }
}

void reset_stopwatch(bool keep_running) {
    bool is_running = started;
    stop_stopwatch();
    elapsed_time = 0;
    start_time = 0;
    last_lap_time = 0;
    last_pebble_time = 0;
    last_period = -1;
    if(is_running && keep_running) start_stopwatch();
    update_stopwatch();

    // Animate all the laps away.
    busy_animating = LAP_TIME_SIZE;
    static PropertyAnimation animations[LAP_TIME_SIZE];
    static GRect targets[LAP_TIME_SIZE];
    for(int i = 0; i < LAP_TIME_SIZE; ++i) {
        shift_lap_layer(&animations[i], &lap_layers[i].layer, &targets[i], LAP_TIME_SIZE);
        animation_schedule(&animations[i].animation);
    }
    next_lap_layer = 0;
    clear_stored_laps();
}

void reset_stopwatch_handler(ClickRecognizerRef recognizer, Window *window) {
    if(busy_animating) return;

    reset_stopwatch(true);
}

void lap_time_handler(ClickRecognizerRef recognizer, Window *window) {
    if(busy_animating) return;
    time_t elapsed = elapsed_time;
    int t = elapsed - last_lap_time;
    last_lap_time = elapsed;
    save_lap_time(t);
}

void update_stopwatch() {
    static char big_time[] = "00:00";
    static char deciseconds_time[] = ".0";
    static char seconds_time[] = ":00";

    // Now convert to hours/minutes/seconds.
    time_t effective_time = current_counter();

    int tenths = (effective_time / 100) % 10;
    int seconds = (effective_time / 1000) % 60;
    int minutes = (effective_time / 60000) % 60;
    int hours = effective_time / 3600000;

    // We can't fit three digit hours, so stop timing here.
    if(hours > 99) {
        stop_stopwatch();
        return;
    }
    if(hours < 1)
    {
        itoa2(minutes, &big_time[0]);
        itoa2(seconds, &big_time[3]);
        itoa1(tenths, &deciseconds_time[1]);
    }
    else
    {
        itoa2(hours, &big_time[0]);
        itoa2(minutes, &big_time[3]);
        itoa2(seconds, &seconds_time[1]);
    }

    // Now draw the strings.
    text_layer_set_text(&big_time_layer, big_time);
    text_layer_set_text(&seconds_time_layer, hours < 1 ? deciseconds_time : seconds_time);
}

void animation_stopped(Animation *animation, void *data) {
    --busy_animating;
}

void shift_lap_layer(PropertyAnimation* animation, Layer* layer, GRect* target, int distance_multiplier) {
    GRect origin = layer_get_frame(layer);
    *target = origin;
    target->origin.y += target->size.h * distance_multiplier;
    property_animation_init_layer_frame(animation, layer, NULL, target);
    animation_set_duration(&animation->animation, 250);
    animation_set_curve(&animation->animation, AnimationCurveLinear);
    animation_set_handlers(&animation->animation, (AnimationHandlers){
        .stopped = (AnimationStoppedHandler)animation_stopped
    }, NULL);
}

void save_lap_time(int lap_time) {
    if(busy_animating) return;

    static PropertyAnimation animations[LAP_TIME_SIZE];
    static GRect targets[LAP_TIME_SIZE];

    // Shift them down visually (assuming they actually exist)
    busy_animating = LAP_TIME_SIZE;
    for(int i = 0; i < LAP_TIME_SIZE; ++i) {
        if(i == next_lap_layer) continue; // This is handled separately.
        shift_lap_layer(&animations[i], &lap_layers[i].layer, &targets[i], 1);
        animation_schedule(&animations[i].animation);
    }

    // Once those are done we can slide our new lap time in.
    format_lap(lap_time, lap_times[next_lap_layer]);

    // Animate it
    static PropertyAnimation entry_animation;
    //static GRect origin; origin = ;
    //static GRect target; target = ;
    property_animation_init_layer_frame(&entry_animation, &lap_layers[next_lap_layer].layer, &GRect(-139, 52, 139, 26), &GRect(5, 52, 139, 26));
    animation_set_curve(&entry_animation.animation, AnimationCurveEaseOut);
    animation_set_delay(&entry_animation.animation, 50);
    animation_set_handlers(&entry_animation.animation, (AnimationHandlers){
        .stopped = (AnimationStoppedHandler)animation_stopped
    }, NULL);
    animation_schedule(&entry_animation.animation);
    next_lap_layer = (next_lap_layer + 1) % LAP_TIME_SIZE;

    // Get it into the laps window, too.
    store_lap_time(lap_time);
}

time_t single_round_running_time() {
    time_t running_time = elapsed_time;
    time_t full_round = round_time + rest_time;
    for (; running_time > full_round; running_time -= full_round);

    return running_time;
}

int get_round_period() {
    // If we are within a round period: 0
    // If we are within a warning period: 1
    // If we are within a resting period: 2

    time_t running_time = single_round_running_time();

    if (running_time < round_time) {
        if (warning_time != 0 && running_time > round_time - warning_time) {
            return 1;
        }
        return 0;
    }
    return 2;
}

time_t current_counter() {
    time_t running_time = single_round_running_time();

    if (running_time < round_time) {
        return round_time - running_time;
    }
    running_time -= round_time;
    return rest_time - running_time;
}

void period_changed() {
    int current_period = get_round_period();

    if (current_period != last_period) {
        if (current_period == 1) {
            vibes_double_pulse();
        }
        else if (current_period == 2) {
            vibes_long_pulse();
        }
        else if (current_period == 0) {
            vibes_long_pulse();
        }
    }
    last_period = current_period;
}

void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie) {
    (void)handle;
    if(cookie == TIMER_UPDATE) {
        if(started) {
            elapsed_time += 100;
            // Every tick of the pebble clock, force our time back to it.
            time_t pebble_time = get_pebble_time();
            if(!last_pebble_time) last_pebble_time = pebble_time;
            if(pebble_time > last_pebble_time) {
                // If it's the first tick, instead of changing our time we calculate the correct time.
                if(!start_time) {
                    start_time = pebble_time - elapsed_time;
                } else {
                    elapsed_time = pebble_time - start_time;
                }
                last_pebble_time = pebble_time;
            }
            update_timer = app_timer_send_event(ctx, elapsed_time <= 3600000 ? 100 : 1000, TIMER_UPDATE);
            period_changed();
        }
        update_stopwatch();
    }
}

void handle_display_lap_times(ClickRecognizerRef recognizer, Window *window) {
    show_laps();
}

void main_config_provider(ClickConfig **config, Window *window) {
    config[BUTTON_RUN]->click.handler = (ClickHandler)toggle_stopwatch_handler;
    config[BUTTON_RESET]->click.handler = (ClickHandler)reset_stopwatch_handler;
    config[BUTTON_LAP]->click.handler = (ClickHandler)lap_time_handler;
    config[BUTTON_LAP]->long_click.handler = (ClickHandler)handle_display_lap_times;
    config[BUTTON_LAP]->long_click.delay_ms = 700;
    (void)window;
}

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .deinit_handler = &handle_deinit,
    .timer_handler = &handle_timer
  };
  app_event_loop(params, &handlers);
}
