#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomtype.h"
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include <cosmo.h>
#include "../sharedmem/sharedmem.h"
#include "../win_stub/win_stub_data.h"
#include "../win_stub/fenster.h"

__static_yoink("zip_uri_support");

#define XMIN(a,b) ((a)<(b)?(a):(b))
ssize_t _copyfd(int in, int out, size_t n) {
  size_t i;
  char buf[512];
  ssize_t dr, dw;
  for (i = 0; i < n; i += dr) {
    dr = read(in, buf, XMIN(n - i, sizeof(buf)));
    if (dr == -1)
      return -1;
    if (!dr)
      break;
    dw = write(out, buf, dr);
    if (dw == -1)
      return -1;
    if (dw != dr) {
      // POSIX requires atomic IO up to PIPE_BUF
      // The minimum permissible PIPE_BUF is 512
      return 0;
    }
  }
  return i;
}


int i_main(int argc, char **argv);

extern boolean quitgame;

uint8_t* p_mem;
struct win_stub_data* p_hdr;
volatile uint8_t* stub_sync;
struct fenster_input_data* inp;
uint32_t* p_pixbuf;
uint32_t size1, size2;
void* hdl1;
void* hdl2;
const int W = DOOMGENERIC_RESX;
const int H = DOOMGENERIC_RESY;

void DG_Init() {
}

void DG_DrawFrame() { 
	while (*stub_sync == 0) { usleep(1000); }

	memcpy(p_pixbuf, DG_ScreenBuffer, size2);
	if (inp->key_down[27] || *stub_sync == 2) {
		*stub_sync = 2;
		quitgame = true;
		usleep(500);
	}
	*stub_sync = 0;
	if (inp->key_mod[0] > 0) { // ctrl key down
		if (inp->key['1']) { // 1x scale 
			p_hdr->win_w = W;
			p_hdr->win_h = H;
			*stub_sync = 3;
		} else if (inp->key['2']) { // 2x scale 
			p_hdr->win_w = W * 2;
			p_hdr->win_h = H * 2;
			*stub_sync = 3;
		} else if (inp->key['3']) { // 4x scale 
			p_hdr->win_w = W * 4;
			p_hdr->win_h = H * 4;
			*stub_sync = 3;
		} else if (inp->key['4']) { // fullscreen 
			p_hdr->win_w = 1;
			p_hdr->win_h = 1;
			*stub_sync = 3;
		}
	}
}

void DG_SleepMs(uint32_t ms) { 
	usleep(ms * 1000); 
}

uint32_t DG_GetTicksMs() {
#ifdef _WIN32
	LARGE_INTEGER freq, count;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);
	return (int64_t)(count.QuadPart * 1000.0 / freq.QuadPart);
#else
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
#endif
}

void DG_SetWindowTitle(const char *title) { 
	(void)title;
}

unsigned char toDoomKey(int k) {
  switch (k) {
  case '\n':
    return KEY_ENTER;
  case '\x1b':
    return KEY_ESCAPE;
  case '\x11':
    return KEY_UPARROW;
  case '\x12':
    return KEY_DOWNARROW;
  case '\x13':
    return KEY_RIGHTARROW;
  case '\x14':
    return KEY_LEFTARROW;
  case 'Z':
    return KEY_FIRE;
  case 'Y':
    return KEY_FIRE;
  case 'X':
    return KEY_RSHIFT;
  case ' ':
    return KEY_USE;
  }
  return tolower(k);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
  static int old[128] = {0};
  for (int i = 0; i < 128; i++) {
    if ((inp->key_down[i] && !old[i]) || (!inp->key_down[i] && old[i])) {
      *pressed = old[i] = inp->key_down[i];
      *doomKey = toDoomKey(i);
      return 1;
    }
  }
  return 0;
}

int extract_stub(const char* zip, const char* to) {
    int fdin, fdout;
    printf("Extracting %s to %s\n", zip, to);
    if ((fdin = open(zip, O_RDONLY | O_CLOEXEC)) == -1) {
        close(fdout);
        unlink(to);
        return -1; // cannot open infile
    }
	unlink(to);
    if ((fdout = open(to, O_WRONLY | O_CREAT | O_EXCL)) == -1) {
        close(fdout);
        unlink(to);
        return -2; // cannot open outfile
    }
    if (_copyfd(fdin, fdout, -1) == -1) {
        close(fdin);
        close(fdout);
        unlink(to);
        return -3; // copy failed
    }
    if (close(fdout)) {
        close(fdin);
        unlink(to);
        return -4; // close outfile failed
    }
    if (close(fdin)) {
        unlink(to);
        return -5; // close infile failed
    }
    return 0; // ok
}

int main(int argc, char **argv) {
	const char* title = "CosmoDOOM";
	const char* mapfile1 = "fstub_exc_doom";
	const char* mapfile2 = "fstub_pix_doom";
	char stub_path[128] = { 0 };
	int i, result;

	uint32_t hdr_sz = sizeof(struct win_stub_data);
	uint32_t inp_sz = sizeof(struct fenster_input_data);
	size1 = hdr_sz + inp_sz;
	size2 = W * H * sizeof(int32_t);

	hdl1 = NULL;
	hdl2 = NULL;
	p_mem = (uint8_t*)create_sharedmem(mapfile1, size1, 1, &hdl1);
	if (p_mem == NULL) {
		printf("Shared memory creation failed for file '%s' -> exit\n", mapfile1); 
		return 1;
	} else {
		printf("Shared memory creation succeeded for file '%s'\n", mapfile1); 
	}
	memset(p_mem, 0, size1);	

	p_pixbuf = (uint32_t*)create_sharedmem(mapfile2, size2, 1, &hdl2);
	if (p_pixbuf == NULL) {
		printf("Shared memory creation failed for file '%s' -> exit\n", mapfile2); 
		return 1;
	} else {
		printf("Shared memory creation succeeded for file '%s'\n", mapfile2); 
	}
	memset(p_pixbuf, 0, size2);	
	
	p_hdr = (struct win_stub_data*)p_mem;
	memcpy(p_hdr->title, title, strlen(title));
	stub_sync = &p_hdr->sync;
	p_hdr->w = W;
	p_hdr->h = H;
	inp = (struct fenster_input_data*)&p_mem[hdr_sz];

	if (IsWindows()) {
		strcpy(stub_path, "./win_stub.exe");
  	} else if (IsXnu()) {
		strcpy(stub_path, "./win_stub_mac");
	} else {
		strcpy(stub_path, "./win_stub_linux");
	}

	pid_t pid = fork();
    if (pid == 0) {
		int result = -1;
		if (IsWindows()) {
			result = extract_stub("/zip/win_stub.exe", stub_path);
	  	} else if (IsXnu()) {
			result = extract_stub("/zip/win_stub_mac", stub_path);
		} else {
			result = extract_stub("/zip/win_stub_linux", stub_path);
		}
		if (result < 0) {
			printf("Error %d while extracting stub -> exit\n", result); 
			*stub_sync = 2;
			return 1;
		}
	    char *argv2[4] = { stub_path, (char *)mapfile1, (char *)mapfile2, NULL };
		if (IsWindows()) {
			char stub_path_par[128] = { 0 };
			strcpy(stub_path_par, stub_path);
			strcat(stub_path_par, " ");
			strcat(stub_path_par, mapfile1);
			strcat(stub_path_par, " ");
			strcat(stub_path_par, mapfile2);
			system(stub_path_par);
		} else {
        	execvp(stub_path, argv2);
		}
    } else {
		printf("Waiting for framebuffer client...");
		while (*stub_sync == 0) { usleep(1000); }
		printf("OK\n");
		if (*stub_sync != 2) {
			i_main(argc, argv);
			*stub_sync = 2;
		}

		result = -1;
		for (int i = 0; i < 10; ++i) {
			result = unlink(stub_path);
			if (result == 0) break;
			usleep(500 * 1000);
		}
		printf("Deleting stub '%s': %d\n", stub_path, result);

		destroy_sharedmem(p_mem, &hdl1);
		destroy_sharedmem(p_pixbuf, &hdl2);
		if (!IsWindows()) {
			for (int i = 0; i < 10; ++i) {
				result = unlink(mapfile1);
				result = unlink(mapfile2);
				if (result == 0) break;
				usleep(500 * 1000);
			}
			printf("Deleting shared memory file '%s': %d\n", mapfile1, result);
			printf("Deleting shared memory file '%s': %d\n", mapfile2, result);
		}

		printf("Exit...\n");
	}
	return 0;
}
