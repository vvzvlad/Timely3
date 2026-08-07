#include <pebble.h>
#include "effects.h"
#include "math.h"
  
  
// { ********* Graphics utility functions (probablu should be seaparated into anothe file?) *********

// set pixel color at given coordinates 
void set_pixel(uint8_t *bitmap_data, int bytes_per_row, int y, int x, uint8_t color) {
      
  #ifdef PBL_COLOR 
    bitmap_data[y*bytes_per_row + x] = color; // in Basalt - simple set entire byte
  #else
    bitmap_data[y*bytes_per_row + x / 8] ^= (-color ^ bitmap_data[y*bytes_per_row + x / 8]) & (1 << (x % 8)); // in Aplite - set the bit
  #endif
}

// get pixel color at given coordinates 
uint8_t get_pixel(uint8_t *bitmap_data, int bytes_per_row, int y, int x) {
  
  #ifdef PBL_COLOR
    return bitmap_data[y*bytes_per_row + x]; // in Basalt - simple get entire byte
  #else
    return (bitmap_data[y*bytes_per_row + x / 8] >> (x % 8)) & 1; // in Aplite - get the bit
  #endif
}

//  ********* Graphics utility functions (probablu should be seaparated into anothe file?) ********* }

  

// inverter effect.
void effect_invert(GContext* ctx,  GRect position, void* param) {
  //capturing framebuffer bitmap
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  uint8_t *bitmap_data =  gbitmap_get_data(fb);
  int bytes_per_row = gbitmap_get_bytes_per_row(fb);

  
  for (int y = 0; y < position.size.h; y++)
     for (int x = 0; x < position.size.w; x++)
        #ifdef PBL_COLOR // on Basalt simple doing NOT on entire returned byte/pixel
          set_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x, ~get_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x));
        #else // on Aplite since only 1 and 0 is returning, doing "not" by 1 - pixel
          set_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x, 1 - get_pixel(bitmap_data, bytes_per_row, y + position.origin.y, x + position.origin.x));
        #endif
 
  graphics_release_frame_buffer(ctx, fb);          
          
}
