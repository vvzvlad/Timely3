/* Timely header file - structs and function prototypes
 * ( to be split out into pieces, for dynamic overlays, at a later date )
 */

// Create a struct to hold our persistent settings...
#include "settings.h"

#include "locale.h"

#include "debug.h"



#include "weather.h"

struct tm *get_time();
void update_date_text();
void update_time_text();
void update_day_text(TextLayer *which_layer);
void update_month_text(TextLayer *which_layer);
void update_week_text(TextLayer *which_layer);
void update_ampm_text(TextLayer *which_layer);
void update_seconds_text(TextLayer *which_layer);
char * get_doy_text();
char * get_dliy_text();
void update_doy_text(TextLayer *which_layer);
void update_dliy_text(TextLayer *which_layer);
void update_doy_dliy_text(TextLayer *which_layer);
void update_timezone_text(TextLayer *which_layer);
void position_connection_layer();
void position_date_layer();
void position_time_layer();
void update_datetime_subtext();
void datetime_layer_update_callback(Layer *me, GContext* ctx);
void statusbar_visible();
void toggle_weather();
void toggle_statusbar();
void battery_layer_update_callback(Layer *me, GContext* ctx);
void set_status_charging_icon();
void generate_vibe(uint32_t vibe_pattern_number);
void update_connection();
bool period_check(uint8_t start_incr, uint8_t stop_incr, bool retval_on_equal);
bool dnd_period_check();
bool hourvibe_period_check();
void set_layer_attr(TextLayer *textlayer, GTextAlignment Alignment);
void set_layer_attr_sfont(TextLayer *textlayer, char *font_key, GTextAlignment Alignment);
void handle_vibe_suppression();
void my_out_sent_handler(DictionaryIterator *sent, void *context);
void my_out_fail_handler(DictionaryIterator *failed, AppMessageResult reason, void *context);
void in_js_ready_handler(DictionaryIterator *received, void *context);
void in_weather_handler(DictionaryIterator *received, void *context);
void in_timezone_handler(DictionaryIterator *received, void *context);
void in_configuration_handler(DictionaryIterator *received, void *context);
void my_in_rcv_handler(DictionaryIterator *received, void *context);
void my_in_drp_handler(AppMessageResult reason, void *context);
int main(void);
