#include <pebble.h>
#include <cl_progressbar.h>

static Window *controlwin;
static Window *pairwin;
static ActionBarLayer *action_bar;
static TextLayer *timertxt;
static TextLayer *tokentxt;
static GBitmap *image_actionbar_play;
static GBitmap *image_actionbar_pause;
static GBitmap *image_actionbar_left;
static GBitmap *image_actionbar_right;
static ProgressBarLayer *progress;
static AppSync controlSync;
static uint8_t controlSyncBuf[1024];
static AppSync pairSync;
static uint8_t pairSyncBuf[1024];
static int32_t timer = 0;
static bool timerrun = false;


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
	}
	snprintf(timeStr, sizeof timeStr, "%02ld : %02ld", (timer / 60) % 100, timer % 60);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "time %s", timeStr);
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
  window_single_click_subscribe(BUTTON_ID_UP, left_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, right_click_handler);
	window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_click_handler_long, NULL);
}

static void sync_changed_handler(const uint32_t key, const Tuple *new_tuple, const Tuple *old_tuple, void *context) {
	switch(key) {
	case SlySlideCount:
		progressbar_layer_set_max(progress, new_tuple->value->int32);
		break;
	case SlySlideNmbr:
		progressbar_layer_set_progress(progress, new_tuple->value->int32);
		break;
	case SlySlideTitle:
		break;
	case SlyToken:
		if(new_tuple->value->cstring[0] == 0)
			text_layer_set_text(tokentxt, "Loading...");
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
		TupletCString(SlySlideTitle, "foobar"),
		TupletInteger(SlyConnected, 1),
	};

	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	bounds.size.w -= ACTION_BAR_WIDTH;
	timertxt = text_layer_create(GRect(0, 0, bounds.size.w, 34));
	text_layer_set_text_color(timertxt, GColorBlack);
	text_layer_set_font(timertxt, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(timertxt, GTextAlignmentCenter);
	text_layer_set_background_color(timertxt, GColorOrange);
	resetTimer();
	layer_add_child(window_layer, text_layer_get_layer(timertxt));

	progress = progressbar_layer_create(GRect(0, bounds.size.h - 16, bounds.size.w, 16));
	progressbar_layer_set_foreground(progress, GColorOrange);
	layer_add_child(window_layer, progressbar_layer_get_layer(progress));

	app_sync_init(&controlSync, controlSyncBuf, sizeof(controlSyncBuf),
			initial_values, ARRAY_LENGTH(initial_values),
			sync_changed_handler, sync_error_handler, NULL);
}

static void controlwin_unload(Window *window) {
	text_layer_destroy(timertxt);
	app_sync_deinit(&controlSync);
	resetTimer();
	sendcmd("disconnect");
}

static void pairwin_load(Window *window) {
	Tuplet initial_values[] = {
		TupletCString(SlyToken, ""),
		TupletInteger(SlyConnected, 0),
	};

	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	tokentxt = text_layer_create(GRect(0, 60, bounds.size.w, 32));
	text_layer_set_text_color(tokentxt, GColorBlack);
	text_layer_set_font(tokentxt, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
	text_layer_set_text_alignment(tokentxt, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(tokentxt));

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
