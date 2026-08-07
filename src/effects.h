#pragma once
#include <pebble.h>

typedef void effect_cb(GContext* ctx, GRect position, void* param);

// inverter effect.
// Added by Yuriy Galanter
effect_cb effect_invert;
