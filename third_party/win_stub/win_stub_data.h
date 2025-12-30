#pragma once
#include <stdint.h>

struct win_stub_data {
	uint32_t w;
	uint32_t h;
	uint8_t sync;
	uint32_t win_w;
	uint32_t win_h;
	uint8_t title[128];
};
