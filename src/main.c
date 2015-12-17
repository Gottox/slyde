#include <pebble.h>
#include <cl_progressbar.h>

static Window *controlwin;
static Window *pairwin;
static ActionBarLayer *action_bar;
static TextLayer *timertxt;
static TextLayer *nexttxt;
static TextLayer *titletxt;
static TextLayer *tokentxt;
static TextLayer *infotxt;
static GBitmap *image_actionbar_play;
static GBitmap *image_actionbar_pause;
static GBitmap *image_actionbar_left;
static GBitmap *image_actionbar_right;
static ProgressBarLayer *progress;
static AppSync controlSync;
static uint8_t controlSyncBuf[1024];
static AppSync pairSync;
static uint8_t pairSyncBuf[1024];
static uint16_t timer = 0;
static bool timerrun = false;

#if defined(PBL_COLOR)
#define HIGHLIGHT_COLOR GColorOrange
#elif defined(PBL_BW)
#define HIGHLIGHT_COLOR GColorBlack
#define action_bar_layer_set_icon_animated(a, b, c, d) action_bar_layer_set_icon(a, b, c)
#endif

enum {
	SlyCmd = 0,
	SlySlideCount = 11,
	SlySlideNmbr = 12,
	SlySlideTitle = 13,
	SlySlideNotes = 14,
	SlyConnected = 15,
	SlyToken = 16
};

static bool showerr(const char *err) {
	return false;
}
static bool sendcmd(const char *str) {
	DictionaryIterator *out;

	if(app_message_outbox_begin(&out) != APP_MSG_OK)
		return showerr("outbox begin failed");

	dict_write_cstring(out, SlyCmd, str);

	if(app_message_outbox_send() != APP_MSG_OK)
		return showerr("outbox send failed");
	return true;
}

static void updateTimer(struct tm *tick_time, TimeUnits units_changed) {
	static char timeStr[16];
	if(tick_time != NULL) {
		timer++;

		// Reset the timer once it hits 100 Minutes
		if(timer % 6000 == 0)
			timer = 0;

		// short pulse every 5 minutes
		if(timer % (60 * 5) == 0)
			vibes_short_pulse();
	}
	snprintf(timeStr, sizeof timeStr, "%02d : %02d", timer / 60, timer % 60);
	text_layer_set_text(timertxt, timeStr);
}

static void startTimer() {
	timerrun = true;
	tick_timer_service_subscribe(SECOND_UNIT, updateTimer);
	action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_SELECT, image_actionbar_pause, true);
}

static void stopTimer() {
	timerrun = false;
	tick_timer_service_unsubscribe();
	action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_SELECT, image_actionbar_play, true);
}

static void resetTimer() {
	stopTimer();
	timer = 0;
	updateTimer(NULL, SECOND_UNIT);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
	if(timerrun)
		stopTimer();
	else
		startTimer();
}

static void select_click_handler_long(ClickRecognizerRef recognizer, void *context) {
	vibes_short_pulse();
	resetTimer();
}

static void left_click_handler(ClickRecognizerRef recognizer, void *context) {
	sendcmd("left");
}

static void right_click_handler(ClickRecognizerRef recognizer, void *context) {
	sendcmd("right");
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
	window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_click_handler_long, NULL);
	window_single_repeating_click_subscribe(BUTTON_ID_UP, 250, left_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 250, right_click_handler);
}

static void sync_changed_handler(const uint32_t key, const Tuple *new_tuple, const Tuple *old_tuple, void *context) {
	switch(key) {
	case SlySlideCount:
		progressbar_layer_set_max(progress, new_tuple->value->int32);
		break;
	case SlySlideNmbr:
		progressbar_layer_set_progress(progress, new_tuple->value->int32);
		startTimer();
		break;
	case SlySlideTitle:
		layer_set_hidden(text_layer_get_layer(nexttxt), new_tuple->value->cstring[0] == 0);
		text_layer_set_text(titletxt, new_tuple->value->cstring);
		break;
	case SlyToken:
		if(new_tuple->value->cstring[0] == 0)
			text_layer_set_text(tokentxt, "****");
		else
			text_layer_set_text(tokentxt, new_tuple->value->cstring);
		break;
	case SlyConnected:
		if(new_tuple->value->int32)
			window_stack_push(controlwin, true);
		else if(window_is_loaded(controlwin))
			window_stack_remove(controlwin, true);
		break;
	}
}

static void sync_error_handler(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
  // An error occured!
  APP_LOG(APP_LOG_LEVEL_ERROR, "sync error %i", app_message_error);
}

static void controlwin_load(Window *window) {
	Tuplet initial_values[] = {
		TupletInteger(SlySlideCount, 100),
		TupletInteger(SlySlideNmbr, 50),
		TupletCString(SlySlideTitle, ""),
		TupletInteger(SlyConnected, 1),
	};

	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	bounds.size.w -= ACTION_BAR_WIDTH;
	progress = progressbar_layer_create(GRect(2, bounds.size.h - 8 - 34, bounds.size.w - 4, 8));
	progressbar_layer_set_foreground(progress, HIGHLIGHT_COLOR);
	layer_add_child(window_layer, progressbar_layer_get_layer(progress));

	timertxt = text_layer_create(GRect(0, bounds.size.h - 34, bounds.size.w, 34));
	text_layer_set_text_color(timertxt, GColorBlack);
	text_layer_set_font(timertxt, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(timertxt, GTextAlignmentCenter);
	text_layer_set_background_color(timertxt, GColorClear);
	resetTimer();
	layer_add_child(window_layer, text_layer_get_layer(timertxt));

	nexttxt = text_layer_create(GRect(0, 0, bounds.size.w, 16));
	text_layer_set_text_color(nexttxt, GColorBlack);
	//text_layer_set_font(nexttxt, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(nexttxt, GTextAlignmentLeft);
	text_layer_set_text(nexttxt, "next:");
	text_layer_set_background_color(nexttxt, GColorClear);
	layer_add_child(window_layer, text_layer_get_layer(nexttxt));

	titletxt = text_layer_create(GRect(0, 16, bounds.size.w, bounds.size.h - 8 - 34 - 16));
	text_layer_set_text_color(titletxt, GColorBlack);
	text_layer_set_font(titletxt, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(titletxt, GTextAlignmentCenter);
	text_layer_set_background_color(titletxt, GColorClear);
	layer_add_child(window_layer, text_layer_get_layer(titletxt));

	app_sync_init(&controlSync, controlSyncBuf, sizeof(controlSyncBuf),
			initial_values, ARRAY_LENGTH(initial_values),
			sync_changed_handler, sync_error_handler, NULL);
	light_enable(true);
	vibes_double_pulse();
}

static void controlwin_unload(Window *window) {
	text_layer_destroy(timertxt);
	app_sync_deinit(&controlSync);
	resetTimer();
	sendcmd("disconnect");
	light_enable(false);
}

static void pairwin_load(Window *window) {
	Tuplet initial_values[] = {
		TupletCString(SlyToken, ""),
		TupletInteger(SlyConnected, 0),
	};

	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	tokentxt = text_layer_create(GRect(0, 30, bounds.size.w, 60));
	text_layer_set_text_color(tokentxt, GColorBlack);
	text_layer_set_font(tokentxt, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
	text_layer_set_text_alignment(tokentxt, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(tokentxt));

	infotxt = text_layer_create(GRect(0, 93, bounds.size.w, 30));
	text_layer_set_text_color(infotxt, GColorBlack);
	//text_layer_set_font(infotxt, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(infotxt, GTextAlignmentCenter);
	text_layer_set_overflow_mode(infotxt, GTextOverflowModeWordWrap);
	text_layer_set_text(infotxt, "more infos at: http://slyde.tox.ninja/");
	layer_add_child(window_layer, text_layer_get_layer(infotxt));

	app_sync_init(&pairSync, pairSyncBuf, sizeof(pairSyncBuf),
			initial_values, ARRAY_LENGTH(initial_values),
			sync_changed_handler, sync_error_handler, NULL);
}

static void pairwin_unload(Window *window) {
	text_layer_destroy(tokentxt);
	app_sync_deinit(&pairSync);
}
static void init(void) {
	// Control Window
  controlwin = window_create();
	window_set_window_handlers(controlwin, (WindowHandlers) {
		.load = controlwin_load,
		.unload = controlwin_unload
	});
	image_actionbar_left = gbitmap_create_with_resource(RESOURCE_ID_LEFT);
	image_actionbar_right = gbitmap_create_with_resource(RESOURCE_ID_RIGHT);
	image_actionbar_play = gbitmap_create_with_resource(RESOURCE_ID_PLAY);
	image_actionbar_pause = gbitmap_create_with_resource(RESOURCE_ID_PAUSE);
  action_bar = action_bar_layer_create();
  action_bar_layer_add_to_window(action_bar, controlwin);
  action_bar_layer_set_click_config_provider(action_bar, click_config_provider);
	action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_UP, image_actionbar_left, true);
	action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_SELECT, image_actionbar_play, true);
	action_bar_layer_set_icon_animated(action_bar, BUTTON_ID_DOWN, image_actionbar_right, true);
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

	// Pair Window
	pairwin = window_create();
	window_set_window_handlers(pairwin, (WindowHandlers) {
		.load = pairwin_load,
		.unload = pairwin_unload
	});
}

static void deinit(void) {
  window_destroy(controlwin);
  action_bar_layer_destroy(action_bar);
	gbitmap_destroy(image_actionbar_left);
	gbitmap_destroy(image_actionbar_right);
}

int main(void) {
  init();
  window_stack_push(pairwin, true);
  app_event_loop();
  deinit();
}
