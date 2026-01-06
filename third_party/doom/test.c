#include "../sharedmem.h"
#include "../fenster_sync.h"

#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef __COSMOCC__
#include "include/libc/dce.h"
__static_yoink("zip_uri_support");
#else
#ifdef _WIN32
static void usleep(__int64 usec) {
	HANDLE timer;
	LARGE_INTEGER period;
	period.QuadPart = -(10 * usec);
	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &period, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}
#endif
#endif

struct fenster_input_data {
	uint8_t key_down[256];          // keys are mostly ASCII, but arrows are 17..20 (persisent until release)
	uint8_t key[256];               // like key_down, but one press signal only (one click)
	uint8_t key_mod[4];             // ctrl, shift, alt, meta
	uint32_t mouse_pos[2];          // mouse x, y
	uint8_t mouse_button[5];        // left, right, middle, scroll up, scroll down (one click)
	uint8_t mouse_button_down[3];   // left, right, middle (persistent until release)
};

uint8_t* p_mem;
struct sync_hdr* p_hdr;
volatile uint8_t* fenster_sync;
struct fenster_input_data* inp;
uint32_t* p_pixbuf;
uint32_t size1, size2;
void* hdl1;
void* hdl2;
const int W = 320;
const int H = 200;

int main(int argc, char **argv) {
	const char* title = "CosmoDOOM";
	const char* mapfile1 = "/tmp/fstub";
	const char* mapfile2 = "/tmp/fstub_pix";
	uint32_t hdr_sz = sizeof(struct sync_hdr);
	uint32_t inp_sz = sizeof(struct fenster_input_data);
	size1 = hdr_sz + inp_sz;
	size2 = W * H * sizeof(int32_t);

	hdl1 = NULL;
	hdl2 = NULL;
	p_mem = (uint8_t*)create_sharedmem(mapfile1, size1, 1, &hdl1);
	if (p_mem == NULL) {
		printf("ERR1 -> exit\n");
		return 1;
	}
	p_pixbuf = (uint32_t*)create_sharedmem(mapfile2, size2, 1, &hdl2);
	if (p_pixbuf == NULL) {
		printf("ERR2 -> exit\n");
		return 1;
	}

	p_hdr = (struct sync_hdr*)p_mem;
	memcpy(p_hdr->title, title, strlen(title));
	fenster_sync = &p_hdr->sync;
	p_hdr->w = W;
	p_hdr->h = H;
	inp = (struct fenster_input_data*)&p_mem[hdr_sz];
 	
#ifdef __COSMOCC__
	if (IsWindows()) {
printf("starting stub\n");
  char exepath[] = "/c/dev/fenster/doom/stub_win.exe";
  char str[2][12];
  char *envs[] = {0};
  char *args[] = {exepath, 0};
  execve(exepath, args, envs);
printf("OK\n");
  	} else if (IsXnu()) {
		open("stub_mac", O_RDONLY);
	} else {
		open("stub_linux", O_RDONLY);
	}
#else
#ifdef _WIN32
	system("start \"\" cmd /c .\\stub_win.exe");
#elif defined(__APPLE__)
	system("./stub_mac");
#else
	system("./stub_linux");
#endif
#endif

	printf("Waiting for framebuffer client\n");
	while (*fenster_sync == 0) { usleep(1000); }
	printf("OK\n");
printf("Waiting for input\n");
char c;
scanf("%c", &c);
	printf("Exit\n");

	destroy_sharedmem(p_mem, &hdl1);
	destroy_sharedmem(p_pixbuf, &hdl2);
	return 0;
}
