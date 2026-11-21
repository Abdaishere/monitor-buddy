// The U8g2 fonts the firmware names, for the host build. Their bytes come
// from olikraus/U8g2 (clib/u8g2_fonts.c); gen_fonts.py copies them out.
#pragma once
#include <stdint.h>
#ifndef U8G2_FONT_SECTION
#define U8G2_FONT_SECTION(name)
#endif
#ifndef U8X8_PROGMEM
#define U8X8_PROGMEM
#endif
#ifdef __cplusplus
extern "C" {
#endif
#include "u8g2_sim_fonts.h"
#ifdef __cplusplus
}
#endif
