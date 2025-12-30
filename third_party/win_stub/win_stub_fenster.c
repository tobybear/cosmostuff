#define FENSTER_IMPLEMENTATION
#include "fenster.h"
#include "win_stub_data.h"
#define SHAREDMEM_PRINT_ERRORS
#include "../sharedmem/sharedmem.h"

#include <stdio.h>

#ifndef _WIN32
#define strcpy_s(a, b, c) strcpy((a), (c));
#endif

int main(int argc, char** argv) {
	struct fenster f = { 0 };
	int64_t now, time;
	const char* mapfile1 = "/tmp/fstub";
	const char* mapfile2 = "/tmp/fstub_pix";
	uint8_t* p_mem;
	struct win_stub_data* p_hdr;
	char title[128] = { 0 };
	volatile uint8_t* fenster_sync;
	struct fenster_input_data* inp;
	uint32_t* p_pixbuf;
	volatile int W, H;
	uint32_t hdr_sz = sizeof(struct win_stub_data);
	uint32_t inp_sz = sizeof(struct fenster_input_data);
	uint32_t size1 = hdr_sz + inp_sz;
	uint32_t size2;
	void* hdl1;
	void* hdl2;
	int fs = 0;

	p_mem = (uint8_t*)create_sharedmem(mapfile1, size1, 0, &hdl1);
	if (!p_mem) { 
		printf("Shared memory creation failed -> exit\n"); 
		return 1;
	}
	p_hdr = (struct win_stub_data*)p_mem;
	fenster_sync = &p_hdr->sync;
	W = p_hdr->w;
	H = p_hdr->h;
	inp = (struct fenster_input_data*)&p_mem[hdr_sz];
	while (W * H == 0) { usleep(100); }
	size2 = W * H * sizeof(int32_t);
	p_pixbuf = (uint32_t*)create_sharedmem(mapfile2, size2, 0, &hdl2);
	
	strcpy_s(title, 128, (const char *)p_hdr->title);
	f.title = title;
	f.width = W;
	f.height = H;
	f.buf = p_pixbuf;
	f.allow_resize = 1;
	fenster_open(&f);
	now = fenster_time();
	while (fenster_loop(&f) == 0) {
		if (*fenster_sync == 2) break;
		if (*fenster_sync == 3) {
			int m = p_hdr->win_w * p_hdr->win_h;
			if (m == 1) {
				fs = 1 - fs;
				fenster_fullscreen(&f, fs);
			} else if (m > 0) {
				fenster_resize(&f, p_hdr->win_w, p_hdr->win_h);
			}
			*fenster_sync = 0;
		}
		*inp = f.inp;

		*fenster_sync = 1;
		while (*fenster_sync == 1) { fenster_sleep(1); }
		time = fenster_time();
		if (time - now < 1000 / 60) {
			fenster_sleep(time - now);
		}
		now = time;
	}
	*fenster_sync = 2;
	fenster_close(&f);
	destroy_sharedmem(p_mem, &hdl1);
	destroy_sharedmem(p_pixbuf, &hdl2);
	return 0;
}

