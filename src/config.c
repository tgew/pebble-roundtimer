/*
 * Pebble Round Timer - Round config window
 * Copyright (C) 2013 Jason Chu
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

#include "common.h"
#include "config.h"

static Window config_window;

static TextLayer round_text_time_layer;
static TextLayer warning_text_time_layer;
static TextLayer rest_text_time_layer;
static TextLayer start_text;
static TextLayer time_selectors[6];
static TextLayer time_separators[3];
int selection = 0;
static char text_digits[6][3];

GFont big_font;

int MAX_TIME = 99*60000+59;

void update_text_digits() {
    itoa2((round_time/1000)/60, text_digits[0]);
    itoa2((round_time/1000)%60, text_digits[1]);
    itoa2((warning_time/1000)/60, text_digits[2]);
    itoa2((warning_time/1000)%60, text_digits[3]);
    itoa2((rest_time/1000)/60, text_digits[4]);
    itoa2((rest_time/1000)%60, text_digits[5]);
}

void redraw_text_digits() {
    for (int i = 0; i < 6; i++) {
        text_layer_set_text(&time_selectors[i], text_digits[i]);
    }
}

void init_text_layer(TextLayer *layer, GRect rect, char *text, GTextAlignment align) {
    text_layer_init(layer, rect);
    text_layer_set_background_color(layer, GColorBlack);
    text_layer_set_font(layer, big_font);
    text_layer_set_text_color(layer, GColorWhite);
    text_layer_set_text(layer, text);
    text_layer_set_text_alignment(layer, align);
}

void update_selections() {
    for (int i = 0; i < 6; i++) {
        if (selection == i) {
            text_layer_set_background_color(&time_selectors[i], GColorWhite);
            text_layer_set_text_color(&time_selectors[i], GColorBlack);
        }
        else {
            text_layer_set_background_color(&time_selectors[i], GColorBlack);
            text_layer_set_text_color(&time_selectors[i], GColorWhite);
        }
    }
    if (selection == 6) {
        text_layer_set_background_color(&start_text, GColorWhite);
        text_layer_set_text_color(&start_text, GColorBlack);
    }
    else {
        text_layer_set_background_color(&start_text, GColorBlack);
        text_layer_set_text_color(&start_text, GColorWhite);
    }
}

void window_appear(Window *window) {
    reset_stopwatch(false);
}

void init_config_window() {
    window_init(&config_window, "Round Timer Config");
    window_stack_push(&config_window, true /* Animated */);
    window_set_background_color(&config_window, GColorBlack);
    window_set_fullscreen(&config_window, false);

    config_window.window_handlers.appear = (WindowHandler)window_appear;

    // Arrange for user input.
    window_set_click_config_provider(&config_window, (ClickConfigProvider) config_config_provider);

    big_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DEJAVU_SANS_SUBSET_18));

    Layer *root_layer = window_get_root_layer(&config_window);

    init_text_layer(&round_text_time_layer, GRect(0, 1, 96, 21), "Round", GTextAlignmentRight);
    layer_add_child(root_layer, &round_text_time_layer.layer);
    init_text_layer(&warning_text_time_layer, GRect(0, 43, 96, 21), "Warning", GTextAlignmentRight);
    layer_add_child(root_layer, &warning_text_time_layer.layer);
    init_text_layer(&rest_text_time_layer, GRect(0, 85, 96, 21), "Rest", GTextAlignmentRight);
    layer_add_child(root_layer, &rest_text_time_layer.layer);
    init_text_layer(&start_text, GRect(53, 127, 43, 21), "Start", GTextAlignmentRight);
    layer_add_child(root_layer, &start_text.layer);

    update_text_digits();
    for (int i = 0; i < 3; i++) {
        int y_height = (i*42)+22;
        init_text_layer(&time_selectors[i*2], GRect(46, y_height, 22, 21), text_digits[i*2], GTextAlignmentRight);
        layer_add_child(root_layer, &time_selectors[i*2].layer);
        init_text_layer(&time_separators[i], GRect(68, y_height, 6, 21), ":", GTextAlignmentRight);
        layer_add_child(root_layer, &time_separators[i].layer);
        init_text_layer(&time_selectors[(i*2)+1], GRect(74, y_height, 22, 21), text_digits[i*2+1], GTextAlignmentRight);
        layer_add_child(root_layer, &time_selectors[(i*2)+1].layer);
    }

    update_selections();
}

void change_selection(int direction) {
    selection += direction;

    if (selection < 0) {
        selection += 7;
    }
    else if (selection >= 7) {
        selection -= 7;
    }

    update_selections();
}

void change_selection_down(ClickRecognizerRef recognizer, Window *window) {
    change_selection(1);
}

void make_watch_go(ClickRecognizerRef recognizer, Window *window) {
    window_stack_push(&main_window, true);
    reset_stopwatch(false);
}

void go_up(ClickRecognizerRef recognizer, Window *window) {
    if (selection == 6) {
        make_watch_go(NULL, NULL);
    }
    else {
        int incr = 1000;
        if (selection % 2 == 0) {
            incr = 60000;
        }
        switch (selection / 2) {
            case 0:
                round_time += incr;
                round_time = (round_time > MAX_TIME) ? MAX_TIME : round_time;
                break;
            case 1:
                warning_time += incr;
                warning_time = (warning_time > MAX_TIME) ? MAX_TIME : warning_time;
                break;
            case 2:
                rest_time += incr;
                rest_time = (rest_time > MAX_TIME) ? MAX_TIME : rest_time;
                break;
        }
        update_text_digits();
        redraw_text_digits();
    }
}

void go_down(ClickRecognizerRef recognizer, Window *window) {
    if (selection == 6) {
        make_watch_go(NULL, NULL);
    }
    else {
        int incr = -1000;
        if (selection % 2 == 0) {
            incr = -60000;
        }
        switch (selection / 2) {
            case 0:
                round_time += incr;
                round_time = (round_time < 0) ? 0 : round_time;
                break;
            case 1:
                warning_time += incr;
                warning_time = (warning_time < 0) ? 0 : warning_time;
                break;
            case 2:
                rest_time += incr;
                rest_time = (rest_time < 0) ? 0 : rest_time;
                break;
        }
        update_text_digits();
        redraw_text_digits();
    }
}

void config_config_provider(ClickConfig **config, Window *window) {
    config[BUTTON_ID_SELECT]->click.handler = (ClickHandler)change_selection_down;

    config[BUTTON_ID_SELECT]->long_click.handler = (ClickHandler)make_watch_go;
    config[BUTTON_ID_SELECT]->long_click.delay_ms = 700;

    config[BUTTON_ID_UP]->click.handler = (ClickHandler)go_up;
    config[BUTTON_ID_UP]->click.repeat_interval_ms = 250;
    config[BUTTON_ID_DOWN]->click.handler = (ClickHandler)go_down;
    config[BUTTON_ID_DOWN]->click.repeat_interval_ms = 250;
    (void)window;
}

void config_run(ClickRecognizerRef recognizer, Window *window) {
    window_stack_push(&main_window, true);
}
