#define FENSTER_IMPLEMENTATION
#include "fenster.h"

int main(int argc, char** argv) {
	struct fenster f = { 0 };
	int64_t now, time;
	uint32_t* p;
	int i;
	int W = 320;
	int H = 200;
	f.title = "Test";
	f.width = W;
	f.height = H;
	f.buf = (uint32_t*)malloc(W * H * 4);
	p = f.buf;
	for (i = 0; i < W * H; ++i) *p++ = ((rand() % 256) << 0) + ((rand() % 256) << 8) + ((rand() % 256) << 16);
	f.allow_resize = 1;
	fenster_open(&f);
	now = fenster_time();
	while (fenster_loop(&f) == 0) {
		if (f.inp.key_down[27]) break;
		time = fenster_time();
		if (time - now < 1000 / 60) {
			fenster_sleep(time - now);
		}
		now = time;
	}
	fenster_close(&f);
	free(f.buf);
	return 0;
}

