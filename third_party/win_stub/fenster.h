#ifndef FENSTER_H
#define FENSTER_H

#define FENSTER_CURSOR
#define FENSTER_STRETCH

#if defined(__APPLE__)
#define FENSTER_MAC
#include <CoreGraphics/CoreGraphics.h>
#include <objc/NSObjCRuntime.h>
#include <objc/objc-runtime.h>
#elif defined(_WIN32)
#define FENSTER_WIN
#include <windows.h>
#define WS_NORESIZE (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU)
static void usleep(__int64 usec) {
	HANDLE timer;
	LARGE_INTEGER period;
	period.QuadPart = -(10 * usec);
	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &period, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}
#else
#define FENSTER_LINUX
#define _DEFAULT_SOURCE 1
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#ifdef FENSTER_CURSOR
#include <X11/cursorfont.h>
#endif
#include <time.h>
#include <string.h>
#include <unistd.h>
#endif

#include <stdint.h>
#include <stdlib.h>

struct fenster_input_data {
	uint8_t key_down[256];          // keys are mostly ASCII, but arrows are 17..20 (persistent until release)
	uint8_t key[256];               // like key_down, but one press signal only (one click)
	uint8_t key_mod[4];             // ctrl, shift, alt, meta
	uint32_t mouse_pos[2];          // mouse x, y
	uint8_t mouse_button[5];        // left, right, middle, scroll up, scroll down (one click)
	uint8_t mouse_button_down[3];   // left, right, middle (persistent until release)
};

struct fenster {
	const char* title;
	int width;
	int height;
	uint32_t* buf;
	struct fenster_input_data inp;
	uint8_t allow_resize;
	int win_width;
	int win_height;
	float scale_x;
	float scale_y;
	int64_t last_sync;   // last sync time
#ifdef FENSTER_STRETCH
	uint32_t* win_buf;
#endif

#if defined(FENSTER_MAC)
	id wnd;
#elif defined(FENSTER_WIN)
	HWND hwnd;
#else
	Display* dpy;
	Window w;
	GC gc;
	XImage* img;
#endif
};

#ifndef FENSTER_API
#define FENSTER_API extern
#endif
FENSTER_API int fenster_open(struct fenster* f);
FENSTER_API int fenster_loop(struct fenster* f);
FENSTER_API void fenster_close(struct fenster* f);
FENSTER_API void fenster_sleep(const int64_t ms);
FENSTER_API int64_t fenster_time(void);
FENSTER_API void fenster_sync(struct fenster* f, const int fps);
FENSTER_API void fenster_fullscreen(struct fenster* f, const int enabled);
FENSTER_API void fenster_resize(struct fenster* f, const int width, const int height);
#ifdef FENSTER_CURSOR
FENSTER_API void fenster_cursor(struct fenster* f, const int type);
#endif // FENSTER_CURSOR
#define fenster_pixel(f, x, y) ((f)->buf[((y) * (f)->width) + (x)])

#ifdef FENSTER_IMPLEMENTATION

#ifdef FENSTER_STRETCH
static void fenster_stretch(struct fenster* f) {
	float yc = 0.f;
	int y, x;
	memset(f->win_buf, 0xff, f->win_width * f->win_height * sizeof(uint32_t));
	for (y = 0; y < f->win_height; y++) {
		uint32_t* p_src = &f->buf[(int)yc * f->width];
		uint32_t* p_dst = &f->win_buf[y * f->win_width];
		float xc = 0.f;
		for (x = 0; x < f->win_width; x++) {
			*p_dst++ = *(p_src + (int)xc);
			xc += f->scale_x;
		}
		yc += f->scale_y;
	}
}
#endif

#ifdef FENSTER_MAC
#define msg(r, o, s) ((r (*)(id, SEL))objc_msgSend)(o, sel_getUid(s))
#define msg1(r, o, s, A, a) ((r (*)(id, SEL, A))objc_msgSend)(o, sel_getUid(s), a)
#define msg2(r, o, s, A, a, B, b) ((r (*)(id, SEL, A, B))objc_msgSend)(o, sel_getUid(s), a, b)
#define msg3(r, o, s, A, a, B, b, C, c) ((r (*)(id, SEL, A, B, C))objc_msgSend)(o, sel_getUid(s), a, b, c)
#define msg4(r, o, s, A, a, B, b, C, c, D, d) ((r (*)(id, SEL, A, B, C, D))objc_msgSend)(o, sel_getUid(s), a, b, c, d)

#define cls(x) ((id)objc_getClass(x))

extern id const NSDefaultRunLoopMode;
extern id const NSApp;
#ifdef FENSTER_CURSOR
extern id NSCursor;
static id cursors[6];
static int current_cursor_type = 1;
#endif // FENSTER_CURSOR

static void fenster_window_resize(id v, SEL s, id note) {
	(void)s;
	struct fenster* f = (struct fenster *)objc_getAssociatedObject(v, "fenster");
	if (f->allow_resize == 0) return;
	CGRect frame = msg(CGRect, msg(id, note, "object"), "frame");
	f->win_width = frame.size.width;
	f->win_height = frame.size.height;
	f->scale_x = (float)f->width / (float)f->win_width;
	f->scale_y = (float)f->height / (float)f->win_height;
#ifdef FENSTER_STRETCH
	f->win_buf = (uint32_t *)realloc(f->win_buf, f->win_width * f->win_height * sizeof(uint32_t));
#endif
}

static void fenster_draw_rect(id v, SEL s, CGRect r) {
	(void)r, (void)s;
	struct fenster* f = (struct fenster *)objc_getAssociatedObject(v, "fenster");
	CGContextRef context = msg(CGContextRef, msg(id, cls("NSGraphicsContext"), "currentContext"), "graphicsPort");
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
#ifdef FENSTER_STRETCH
	if (f->width == f->win_width && f->height == f->win_height) {
		memcpy(f->win_buf, f->buf, f->width * f->height * sizeof(uint32_t));
	} else {
		fenster_stretch(f);
	}
	CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, f->win_buf, f->win_width * f->win_height * 4, NULL);
	CGImageRef img = CGImageCreate(f->win_width, f->win_height, 8, 32, f->win_width * 4, space,
		(CGBitmapInfo)kCGImageAlphaNoneSkipFirst | (CGBitmapInfo)kCGBitmapByteOrder32Little,
		provider, NULL, false, kCGRenderingIntentDefault);
#else
	CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, f->buf, f->width * f->height * 4, NULL);
	CGImageRef img = CGImageCreate(f->width, f->height, 8, 32, f->width * 4, space,
		(CGBitmapInfo)kCGImageAlphaNoneSkipFirst | (CGBitmapInfo)kCGBitmapByteOrder32Little,
		provider, NULL, false, kCGRenderingIntentDefault);
#endif // FENSTER_STRETCH
	CGColorSpaceRelease(space);
	CGDataProviderRelease(provider);
	CGContextDrawImage(context, CGRectMake(0, 0, f->win_width, f->win_height), img);
	CGImageRelease(img);
}

static BOOL fenster_should_close(id v, SEL s, id w) {
	(void)v, (void)s, (void)w;
	msg1(void, NSApp, "terminate:", id, NSApp);
	return YES;
}

// https://medium.com/@colleagueriley/rgfw-under-the-hood-cocoa-in-pure-c-2e2ab559f657
FENSTER_API int fenster_open(struct fenster* f) {
	f->win_width = f->width;
	f->win_height = f->height;
	f->scale_x = 1.f;
	f->scale_y = 1.f;
#ifdef FENSTER_STRETCH
	if (f->win_buf) free(f->win_buf);
	f->win_buf = (uint32_t*)malloc(f->win_width * f->win_height * sizeof(uint32_t));
#endif

	msg(id, cls("NSApplication"), "sharedApplication");
	msg1(void, NSApp, "setActivationPolicy:", NSInteger, 0);
	int style_mask = f->allow_resize ? 15 : 7; // 3
	f->wnd = msg4(id, msg(id, cls("NSWindow"), "alloc"), "initWithContentRect:styleMask:backing:defer:",
		CGRect, CGRectMake(0, 0, f->width, f->height), NSUInteger, style_mask, NSUInteger, 2, BOOL, NO);
	Class windelegate = objc_allocateClassPair((Class)cls("NSObject"), "FensterDelegate", 0);
	class_addMethod(windelegate, sel_getUid("windowShouldClose:"), (IMP)fenster_should_close, "c@:@");
	objc_registerClassPair(windelegate);
	msg1(void, f->wnd, "setDelegate:", id, msg(id, msg(id, (id)windelegate, "alloc"), "init"));
	Class c = objc_allocateClassPair((Class)cls("NSView"), "FensterView", 0);
	class_addMethod(c, sel_getUid("drawRect:"), (IMP)fenster_draw_rect, "i@:@@");
	objc_registerClassPair(c);

	id v = msg(id, msg(id, (id)c, "alloc"), "init");
	msg1(void, f->wnd, "setContentView:", id, v);
	objc_setAssociatedObject(v, "fenster", (id)f, OBJC_ASSOCIATION_ASSIGN);

	id title = msg1(id, cls("NSString"), "stringWithUTF8String:", const char *, f->title);
	msg1(void, f->wnd, "setTitle:", id, title);
	msg1(void, f->wnd, "makeKeyAndOrderFront:", id, nil);
	msg(void, f->wnd, "center");
	msg1(void, NSApp, "activateIgnoringOtherApps:", BOOL, YES);
	class_addMethod(c, sel_getUid("windowDidResize:"), (IMP)fenster_window_resize, "v@:@");
	msg4(void, msg(id, cls("NSNotificationCenter"), "defaultCenter"), "addObserver:selector:name:object:",
		id, v, SEL, sel_getUid("windowDidResize:"), id, msg1(id, cls("NSString"), 
		"stringWithUTF8String:", const char*, "NSWindowDidResizeNotification"), id, f->wnd);
	return 0;
}

FENSTER_API void fenster_close(struct fenster* f) {
#ifdef FENSTER_CURSOR
	// Ensure cursor is visible when closing
	msg(void, cls("NSCursor"), "unhide");
#endif // FENSTER_CURSOR
	msg(void, f->wnd, "close");
#ifdef FENSTER_STRETCH
	free(f->win_buf); 
	f->win_buf = NULL;
#endif
}

// clang-format off
static const uint8_t FENSTER_KEYCODES[128] = {65,83,68,70,72,71,90,88,67,86,0,66,81,87,69,82,89,84,49,50,51,52,54,53,61,57,55,45,56,48,93,79,85,91,73,80,10,76,74,39,75,59,92,44,47,78,77,46,9,32,96,8,0,27,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,26,2,3,127,0,5,0,4,0,20,19,18,17,0};
// clang-format on
FENSTER_API int fenster_loop(struct fenster* f) {
	msg1(void, msg(id, f->wnd, "contentView"), "setNeedsDisplay:", BOOL, YES);
	id ev = msg4(id, NSApp, "nextEventMatchingMask:untilDate:inMode:dequeue:", NSUInteger, NSUIntegerMax, id, NULL, id, NSDefaultRunLoopMode, BOOL, YES);
	memset(f->inp.mouse_button, 0, sizeof(f->inp.mouse_button));
	memset(f->inp.key, 0, sizeof(f->inp.key));
	if (!ev) return 0;
	NSUInteger evtype = msg(NSUInteger, ev, "type");
	switch (evtype) {
	case 1:
		{
			/* NSEventTypeLeftMouseDown */
			f->inp.mouse_button[0] = f->inp.mouse_button_down[0] = 1;
			break;
		}
	case 2:
		{
			/* NSEventTypeLeftMouseUp */
			f->inp.mouse_button_down[0] = 0;
			break;
		}
	case 3:
		{
			/* NSEventTypeRightMouseDown */
			f->inp.mouse_button[1] = f->inp.mouse_button_down[1] = 1;
			break;
		}
	case 4:
		{
			/* NSEventTypeRightMouseUp */
			f->inp.mouse_button_down[1] = 0;
			break;
		}
	case 25:
		{
			/* NSEventTypeOtherMouseDown */
			f->inp.mouse_button[2] = f->inp.mouse_button_down[2] = 1;
			break;
		}
	case 26:
		{
			/* NSEventTypeOtherMouseUp */
			f->inp.mouse_button_down[2] = 0;
			break;
		}
	case 22: /* NSEventTypeScrollWheel */
		{
			if (msg(CGFloat, ev, "scrollingDeltaY") > 0) {
				f->inp.mouse_button[3] = 1;
			} else if (msg(CGFloat, ev, "scrollingDeltaY") < 0) {
				f->inp.mouse_button[4] = 1;
			}
			break;
		}
	case 5:
	case 6: 
		{ /* NSEventTypeMouseMoved */
			CGPoint xy = msg(CGPoint, ev, "locationInWindow");
			f->inp.mouse_pos[0] = (uint32_t)((float)xy.x * f->scale_x);
			f->inp.mouse_pos[1] = (uint32_t)((float)(f->height - xy.y) * f->scale_y);
			return 0;
		}
	case 10: /*NSEventTypeKeyDown*/
		{
			NSUInteger k = msg(NSUInteger, ev, "keyCode");
			if (k < 127) {
				int key = FENSTER_KEYCODES[k];
				f->inp.key[key] = 1;
				f->inp.key_down[key] = 1;
			}
			NSUInteger mod = msg(NSUInteger, ev, "modifierFlags") >> 17;
			f->inp.key_mod[0] = (mod & 2) ? 1 : 0;      // Shift
			f->inp.key_mod[1] = (mod & 1) ? 1 : 0;      // Control
			f->inp.key_mod[2] = (mod & 4) ? 1 : 0;      // Alt/Option
			f->inp.key_mod[3] = (mod & 8) ? 1 : 0;      // Command
			return 0;
		}
	case 11: /*NSEventTypeKeyUp*/ 
		{
			NSUInteger k = msg(NSUInteger, ev, "keyCode");
			if (k < 127) {
				int key = FENSTER_KEYCODES[k];
				f->inp.key_down[key] = 0;
			}
			NSUInteger mod = msg(NSUInteger, ev, "modifierFlags") >> 17;
			f->inp.key_mod[0] = (mod & 2) ? 1 : 0;      // Shift
			f->inp.key_mod[1] = (mod & 1) ? 1 : 0;      // Control
			f->inp.key_mod[2] = (mod & 4) ? 1 : 0;      // Alt/Option
			f->inp.key_mod[3] = (mod & 8) ? 1 : 0;      // Command
			return 0;
		}
	}
	msg1(void, NSApp, "sendEvent:", id, ev);
	return 0;
}

FENSTER_API void fenster_fullscreen(struct fenster* f, const int enabled) {
	// Toggle fullscreen using NSWindow's toggleFullScreen: method
	msg1(void, f->wnd, "toggleFullScreen:", id, nil);

	// Update window style mask based on fullscreen state
	NSUInteger styleMask = msg(NSUInteger, f->wnd, "styleMask");
	if (enabled) {
		styleMask |= 1 << 14;  // NSWindowStyleMaskFullScreen
	} else {
		styleMask &= ~(1 << 14);
	}
	msg1(void, f->wnd, "setStyleMask:", NSUInteger, styleMask);
}

#ifdef FENSTER_CURSOR
FENSTER_API void fenster_cursor(struct fenster* f, const int type) {
	if (type == current_cursor_type) return;
	// Initialize cursors on first use
	if (!cursors[1]) {
		// Hide cursor (NULL represents hidden cursor)
		cursors[0] = NULL;
		// Normal arrow cursor
		cursors[1] = msg(id, cls("NSCursor"), "arrowCursor");
		// Pointer/hand cursor
		cursors[2] = msg(id, cls("NSCursor"), "pointingHandCursor");
		// Progress/wait cursor
		cursors[3] = msg(id, cls("NSCursor"), "operationNotAllowedCursor");
		// Crosshair cursor
		cursors[4] = msg(id, cls("NSCursor"), "crosshairCursor");
		// Text cursor
		cursors[5] = msg(id, cls("NSCursor"), "IBeamCursor");
	}

	if (type == 0) {
		// Hide the cursor
		msg(void, cls("NSCursor"), "hide");
	} else {
		// Show the cursor if it was hidden
		msg(void, cls("NSCursor"), "unhide");
		// Set and push the new cursor
		msg(void, cursors[type], "set");
	}
	current_cursor_type = type;
}
#endif // FENSTER_CURSOR

FENSTER_API void fenster_resize(struct fenster* f, const int width, const int height) {
	if (f->allow_resize == 0) return;
	CGRect newFrame = CGRectMake(0, 0, width, height);
	msg2(void, f->wnd, "setFrame:display:", CGRect, newFrame, BOOL, YES);
	msg(void, f->wnd, "center");
}

#endif // FENSTER_MAC

#ifdef FENSTER_WIN
// clang-format off
static const uint8_t FENSTER_KEYCODES[] = {0,27,49,50,51,52,53,54,55,56,57,48,45,61,8,9,81,87,69,82,84,89,85,73,79,80,91,93,10,0,65,83,68,70,71,72,74,75,76,59,39,96,0,92,90,88,67,86,66,78,77,44,46,47,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,17,3,0,20,0,19,0,5,18,4,26,127};
// clang-format on

static WINDOWPLACEMENT g_wpPrev = { 
	sizeof(WINDOWPLACEMENT), // length
	0,                       // flags
	SW_NORMAL,               // showCmd
	{0, 0},                  // ptMinPosition
	{0, 0},                  // ptMaxPosition
	{0, 0, 0, 0}             // rcNormalPosition
};

#ifdef FENSTER_CURSOR
static HCURSOR cursors[6];
static int cursors_initialized = 0;
static int current_cursor_type = 1;
#endif // FENSTER_CURSOR

typedef struct BINFO {
	BITMAPINFOHEADER bmiHeader;
	RGBQUAD bmiColors[3];
} BINFO;

static LRESULT CALLBACK fenster_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	struct fenster* f = (struct fenster *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	int new_width, new_height, key;
	switch (msg) {
	case WM_SIZE:
		{
			RECT rect;
			GetClientRect(hwnd, &rect);
			new_width = rect.right - rect.left;
			new_height = rect.bottom - rect.top;
			if (new_width != f->win_width || new_height != f->win_height) {
				f->win_width = new_width;
				f->win_height = new_height;
				f->scale_x = (float)f->width / (float)f->win_width;
				f->scale_y = (float)f->height / (float)f->win_height;
#ifdef FENSTER_STRETCH
				f->win_buf = (uint32_t *)realloc(f->win_buf, new_width * new_height * sizeof(uint32_t));
#endif
			}
			break;
		}
	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			HDC memdc = CreateCompatibleDC(hdc);

#ifdef FENSTER_STRETCH
			HBITMAP hbmp = CreateCompatibleBitmap(hdc, f->win_width, f->win_height);
			HBITMAP oldbmp = (HBITMAP)SelectObject(memdc, hbmp);
			BINFO bi = {{sizeof(bi), f->win_width, -f->win_height, 1, 32, BI_BITFIELDS}};
			bi.bmiColors[0].rgbRed = 0xff;
			bi.bmiColors[1].rgbGreen = 0xff;
			bi.bmiColors[2].rgbBlue = 0xff;
			if (f->width == f->win_width && f->height == f->win_height) {
				SetDIBitsToDevice(memdc, 0, 0, f->win_width, f->win_height, 0, 0, 0, f->win_height, f->buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
			} else {
				fenster_stretch(f);
				SetDIBitsToDevice(memdc, 0, 0, f->win_width, f->win_height, 0, 0, 0, f->win_height, f->win_buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
			}
			BitBlt(hdc, 0, 0, f->win_width, f->win_height, memdc, 0, 0, SRCCOPY);
#else
			HBITMAP hbmp = CreateCompatibleBitmap(hdc, f->width, f->height);
			HBITMAP oldbmp = (HBITMAP)SelectObject(memdc, hbmp);
			BINFO bi = {{sizeof(bi), f->width, -f->height, 1, 32, BI_BITFIELDS}};
			bi.bmiColors[0].rgbRed = 0xff;
			bi.bmiColors[1].rgbGreen = 0xff;
			bi.bmiColors[2].rgbBlue = 0xff;
			SetDIBitsToDevice(memdc, 0, 0, f->width, f->height, 0, 0, 0, f->height, f->buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
			BitBlt(hdc, 0, 0, f->win_width, f->win_height, memdc, 0, 0, SRCCOPY);
			/*
			if (f->width == f->win_width && f->height == f->win_height) {
				BitBlt(hdc, 0, 0, f->win_width, f->win_height, memdc, 0, 0, SRCCOPY);
			} else {
				StretchBlt(hdc, 0, 0, f->win_width, f->win_height, memdc, 0, 0, f->width, f->height, SRCCOPY);
			}
			*/
#endif // FENSTER_STRETCH
			SelectObject(memdc, oldbmp);
			DeleteObject(hbmp);
			DeleteDC(memdc);
			EndPaint(hwnd, &ps);
			break;
		}
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;
	case WM_LBUTTONDOWN:
		f->inp.mouse_button[0] = f->inp.mouse_button_down[0] = 1;
		break;
	case WM_LBUTTONUP:
		f->inp.mouse_button_down[0] = 0;
		break;
	case WM_RBUTTONDOWN:
		f->inp.mouse_button[1] = f->inp.mouse_button_down[1] = 1;
		break;
	case WM_RBUTTONUP:
		f->inp.mouse_button_down[1] = 0;
		break;
	case WM_MBUTTONDOWN:
		f->inp.mouse_button[2] = f->inp.mouse_button_down[2] = 1;
		break;
	case WM_MBUTTONUP:
		f->inp.mouse_button_down[2] = 0;
		break;
	case WM_MOUSEWHEEL:
		{
			if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
				f->inp.mouse_button[3] = 1;
			} else {
				f->inp.mouse_button[4] = 1;
			}
			break;
		}
	case WM_MOUSEMOVE:
		{
			f->inp.mouse_pos[1] = (uint32_t)((float)HIWORD(lParam) * f->scale_y);
			f->inp.mouse_pos[0] = (uint32_t)((float)LOWORD(lParam) * f->scale_x);
		}
		break;
	case WM_KEYDOWN:
		{
			f->inp.key_mod[0] = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
			f->inp.key_mod[1] = (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
			f->inp.key_mod[2] = (GetKeyState(VK_MENU) & 0x8000) ? 1 : 0;
			f->inp.key_mod[3] = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? 1 : 0;
			key = FENSTER_KEYCODES[HIWORD(lParam) & 0x1ff];
			f->inp.key[key] = 1;
			f->inp.key_down[key] = 1;
			break;
		}
	case WM_KEYUP:
		{
			f->inp.key_mod[0] = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
			f->inp.key_mod[1] = (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0;
			f->inp.key_mod[2] = (GetKeyState(VK_MENU) & 0x8000) ? 1 : 0;
			f->inp.key_mod[3] = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? 1 : 0;
			key = FENSTER_KEYCODES[HIWORD(lParam) & 0x1ff];
			f->inp.key_down[key] = 0;
			break;
		}
	case WM_DESTROY:
		{
			PostQuitMessage(0);
			break;
		}
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

#include <stdio.h>
FENSTER_API int fenster_open(struct fenster* f) {
	int win_style, adjusted_width, adjusted_height;
	size_t title_len = strlen(f->title);
	RECT desired_rect = {0, 0, f->width, f->height};
	char* title_w;
	int i;
	int j = 0;

	HINSTANCE hInstance = GetModuleHandle(NULL);
	WNDCLASSEX wc = {0};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_VREDRAW | CS_HREDRAW;
	wc.lpfnWndProc = fenster_wndproc;
	wc.hInstance = hInstance;

#ifndef FENSTER_STRETCH
	f->allow_resize = 0;
#endif // !FENSTER_STRETCH
	title_w = (char*)malloc(title_len * 2 + 2);
	memset(title_w, 0, title_len * 2 + 2);
	f->win_width = f->width;
	f->win_height = f->height;
#ifdef FENSTER_STRETCH
	if (f->win_buf) free(f->win_buf);
	f->win_buf = (uint32_t*)malloc(f->win_width * f->win_height * sizeof(uint32_t));
#endif
	f->scale_x = 1.f;
	f->scale_y = 1.f;
	for (i = 0; i < title_len; i++) {
		title_w[j] = f->title[i];
		j += 2;
	}
	wc.lpszClassName = (LPCWSTR)title_w;
	RegisterClassEx(&wc);
	win_style = f->allow_resize ? WS_OVERLAPPEDWINDOW : WS_NORESIZE;
	AdjustWindowRectEx(&desired_rect, win_style, FALSE, WS_EX_CLIENTEDGE);
	adjusted_width = desired_rect.right - desired_rect.left;
	adjusted_height = desired_rect.bottom - desired_rect.top;
	f->hwnd = CreateWindowEx(0, (LPCWSTR)title_w, (LPCWSTR)title_w, win_style, CW_USEDEFAULT, CW_USEDEFAULT,
		adjusted_width, adjusted_height, NULL, NULL, hInstance, NULL);
	if (f->hwnd == NULL) return -1;
	SetWindowLongPtr(f->hwnd, GWLP_USERDATA, (LONG_PTR)f);
	ShowWindow(f->hwnd, SW_NORMAL);
	UpdateWindow(f->hwnd);
	free(title_w);
	return 0;
}

FENSTER_API void fenster_close(struct fenster* f) { 
	PostMessage(f->hwnd, WM_CLOSE, 0, 0);
#ifdef FENSTER_CURSOR
	// Ensure cursor is visible when closing
	while (ShowCursor(TRUE) < 0);
#endif // FENSTER_CURSOR
#ifdef FENSTER_STRETCH
	free(f->win_buf); 
	f->win_buf = NULL;
#endif
}

FENSTER_API int fenster_loop(struct fenster* f) {
	MSG msg;
	memset(f->inp.mouse_button, 0, sizeof(f->inp.mouse_button));
	memset(f->inp.key, 0, sizeof(f->inp.key));
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) return -1;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	InvalidateRect(f->hwnd, NULL, TRUE);
	return 0;
}

FENSTER_API void fenster_sleep(const int64_t ms) { 
	// Sleep((DWORD)ms); 
	usleep(ms * 1000);
}

FENSTER_API int64_t fenster_time() {
	LARGE_INTEGER freq, count;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);
	return (int64_t)(count.QuadPart * 1000.0 / freq.QuadPart);
}

FENSTER_API void fenster_fullscreen(struct fenster* f, const int enabled) {
	DWORD dwStyle = GetWindowLong(f->hwnd, GWL_STYLE);
	MONITORINFO mi = { 
		sizeof(MONITORINFO),           // cbSize
		{0, 0, 0, 0},                 // rcMonitor
		{0, 0, 0, 0},                 // rcWork
		MONITORINFOF_PRIMARY          // dwFlags
	};
	int ws = f->allow_resize ? WS_OVERLAPPEDWINDOW : WS_NORESIZE;

	if (enabled) {
		GetWindowPlacement(f->hwnd, &g_wpPrev);
		SetWindowLong(f->hwnd, GWL_STYLE, dwStyle & ~ws);
		GetMonitorInfo(MonitorFromWindow(f->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);

		SetWindowPos(f->hwnd, HWND_TOP,
			mi.rcMonitor.left, mi.rcMonitor.top,
			mi.rcMonitor.right - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	} else {
		SetWindowLong(f->hwnd, GWL_STYLE, dwStyle | ws);
		SetWindowPlacement(f->hwnd, &g_wpPrev);
		SetWindowPos(f->hwnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

#ifdef FENSTER_CURSOR
FENSTER_API void fenster_cursor(struct fenster* f, const int type) {
	int t = type;
	WNDCLASSEXA wc;

	if (type == current_cursor_type) return;
	// Initialize cursors on first use
	if (!cursors_initialized) {
		cursors[0] = NULL;  // Will be used for hidden cursor
		cursors[1] = LoadCursor(NULL, IDC_ARROW);      // Normal arrow
		cursors[2] = LoadCursor(NULL, IDC_HAND);       // Pointer/hand
		cursors[3] = LoadCursor(NULL, IDC_WAIT);       // Progress/wait
		cursors[4] = LoadCursor(NULL, IDC_CROSS);      // Crosshair
		cursors[5] = LoadCursor(NULL, IDC_IBEAM);      // Text
		cursors_initialized = 1;
	}

	if (type < 0 || type > 5) {
		t = 1;  // Default to normal cursor if invalid type
	}

	if (t == 0) {
		// Hide cursor
		while (ShowCursor(FALSE) >= 0);
	} else {
		// Show cursor if it was hidden
		while (ShowCursor(TRUE) < 0);

		// Set the cursor
		SetCursor(cursors[t]);

		// Also set the class cursor so it persists
		wc.cbSize = sizeof(WNDCLASSEXA);
		GetClassInfoExA(GetModuleHandle(NULL), f->title, &wc);
		wc.hCursor = cursors[t];
		SetClassLongPtr(f->hwnd, GCLP_HCURSOR, (LONG_PTR)cursors[t]);
	}
	current_cursor_type = t;
}
#endif // FENSTER_CURSOR

FENSTER_API void fenster_resize(struct fenster* f, const int width, const int height) {
	RECT rect = { 0, 0, width, height };
	int win_style = f->allow_resize ? WS_OVERLAPPEDWINDOW : WS_NORESIZE;
	if (f->allow_resize == 0) return;
	AdjustWindowRectEx(&rect, win_style, FALSE, 0);
	SetWindowPos(f->hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
}

#endif // FENSTER_WIN

#ifdef FENSTER_LINUX
// clang-format off
static int FENSTER_KEYCODES[124] = {XK_BackSpace,8,XK_Delete,127,XK_Down,18,XK_End,5,XK_Escape,27,XK_Home,2,XK_Insert,26,XK_Left,20,XK_Page_Down,4,XK_Page_Up,3,XK_Return,10,XK_Right,19,XK_Tab,9,XK_Up,17,XK_apostrophe,39,XK_backslash,92,XK_bracketleft,91,XK_bracketright,93,XK_comma,44,XK_equal,61,XK_grave,96,XK_minus,45,XK_period,46,XK_semicolon,59,XK_slash,47,XK_space,32,XK_a,65,XK_b,66,XK_c,67,XK_d,68,XK_e,69,XK_f,70,XK_g,71,XK_h,72,XK_i,73,XK_j,74,XK_k,75,XK_l,76,XK_m,77,XK_n,78,XK_o,79,XK_p,80,XK_q,81,XK_r,82,XK_s,83,XK_t,84,XK_u,85,XK_v,86,XK_w,87,XK_x,88,XK_y,89,XK_z,90,XK_0,48,XK_1,49,XK_2,50,XK_3,51,XK_4,52,XK_5,53,XK_6,54,XK_7,55,XK_8,56,XK_9,57};
// clang-format on
static Atom wm_delete_window;
static Atom wm_state;
static Atom wm_fullscreen;
#ifdef FENSTER_CURSOR
static Cursor cursors[6];
static int cursors_initialized = 0;
static int current_cursor_type = 1;
#endif // FENSTER_CURSOR

FENSTER_API int fenster_open(struct fenster* f) {
	f->win_width = f->width;
	f->win_height = f->height;
#ifdef FENSTER_STRETCH
	if (f->win_buf) free(f->win_buf);
	f->win_buf = (uint32_t*)malloc(f->win_width * f->win_height * sizeof(uint32_t));
#endif
	f->scale_x = 1.f;
	f->scale_y = 1.f;

	f->dpy = XOpenDisplay(NULL);
	int screen = DefaultScreen(f->dpy);
	f->w = XCreateSimpleWindow(f->dpy, RootWindow(f->dpy, screen), 0, 0, f->width,
		f->height, 0, BlackPixel(f->dpy, screen), WhitePixel(f->dpy, screen));

	if (f->allow_resize == 0) {
		XSizeHints hints = { 0 };
		// disable resizing
		hints.flags = PMinSize | PMaxSize;
		hints.min_width = f->width;
		hints.min_height = f->height;
		hints.max_width = f->width;
		hints.max_height = f->height;
		XSetWMNormalHints(f->dpy, f->w, &hints);
	}

	f->gc = XCreateGC(f->dpy, f->w, 0, 0);
	wm_delete_window = XInternAtom(f->dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(f->dpy, f->w, &wm_delete_window, 1);
	wm_state = XInternAtom(f->dpy, "_NET_WM_STATE", False);
	wm_fullscreen = XInternAtom(f->dpy, "_NET_WM_STATE_FULLSCREEN", False);

	XSelectInput(f->dpy, f->w, ExposureMask | KeyPressMask | KeyReleaseMask | 
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
	XStoreName(f->dpy, f->w, f->title);
	XMapWindow(f->dpy, f->w);
	XSync(f->dpy, f->w);
	f->img = XCreateImage(f->dpy, DefaultVisual(f->dpy, 0), 24, ZPixmap, 0, (char *)f->buf, f->width, f->height, 32, 0);
	return 0;
}

FENSTER_API void fenster_close(struct fenster* f) { 
	XDestroyWindow(f->dpy, f->w);
#ifdef FENSTER_CURSOR
	if (cursors_initialized) {
		for (int i = 1; i <= 5; i++) {
			XFreeCursor(f->dpy, cursors[i]);
		}
		cursors_initialized = 0;
	}
#endif // FENSTER_CURSOR
	XCloseDisplay(f->dpy);
#ifdef FENSTER_STRETCH
	free(f->win_buf); 
	f->win_buf = NULL;
#endif
}

FENSTER_API int fenster_loop(struct fenster* f) {
	XEvent ev;
#ifdef FENSTER_STRETCH
	if (f->width == f->win_width && f->height == f->win_height) {
		memcpy(f->win_buf, f->buf, f->width * f->height * sizeof(uint32_t));
	} else {
		fenster_stretch(f);
	}
	XPutImage(f->dpy, f->w, f->gc, f->img, 0, 0, 0, 0, f->win_width, f->win_height);
#else
	XPutImage(f->dpy, f->w, f->gc, f->img, 0, 0, 0, 0, f->width, f->height);
#endif // FENSTER_STRETCH
	XFlush(f->dpy);
	memset(f->inp.mouse_button, 0, sizeof(f->inp.mouse_button));
	memset(f->inp.key, 0, sizeof(f->inp.key));
	while (XPending(f->dpy)) {
		XNextEvent(f->dpy, &ev);
		switch (ev.type) {
		case ConfigureNotify:
			{
				if (ev.xconfigure.width != f->win_width || ev.xconfigure.height != f->win_height) {
					f->win_width = ev.xconfigure.width;
					f->win_height = ev.xconfigure.height;
					f->scale_x = (float)f->width / (float)f->win_width;
					f->scale_y = (float)f->height / (float)f->win_height;
#ifdef FENSTER_STRETCH
					f->win_buf = (uint32_t *)realloc(f->win_buf, f->win_width * f->win_height * sizeof(uint32_t));
					f->img->data = NULL;
					XDestroyImage(f->img);
					f->img = XCreateImage(f->dpy, DefaultVisual(f->dpy, 0), 24, ZPixmap, 0, (char *)f->win_buf, f->win_width, f->win_height, 32, 0);
#endif
				}
				break;
			}
		case ClientMessage:
			{
				if ((Atom)ev.xclient.data.l[0] == wm_delete_window) {
					return 1;
				} 
				break;
			}
		case ButtonPress:
			{
				switch (ev.xbutton.button) 
				{
				case Button1: 
					{
						f->inp.mouse_button[0] = f->inp.mouse_button_down[0] = 1; 
						break;
					}
				case Button3: 
					{
						f->inp.mouse_button[1] = f->inp.mouse_button_down[1] = 1; 
						break;
					}
				case Button2: 
					{
						f->inp.mouse_button[2] = f->inp.mouse_button_down[2] = 1; 
						break;
					}
				case Button4: 
					{
						f->inp.mouse_button[3] = 1; 
						break;
					}
				case Button5: 
					{
						f->inp.mouse_button[4] = 1; 
						break;
					}
				}
				break;
			}
		case ButtonRelease:
			{
				switch (ev.xbutton.button)
				{
				case Button1:
					{
						f->inp.mouse_button_down[0] = 0; 
						break; 
					}
				case Button3:
					{
						f->inp.mouse_button_down[1] = 0; 
						break; 
					}
				case Button2:
					{
						f->inp.mouse_button_down[2] = 0; 
						break; 
					}
				}
				break;
			}
		case MotionNotify:
			{
				f->inp.mouse_pos[0] = (uint32_t)((float)ev.xmotion.x * f->scale_x);
				f->inp.mouse_pos[1] = (uint32_t)((float)ev.xmotion.y * f->scale_y);
				break;
			}
		case KeyPress:
			{
				int m = ev.xkey.state;
				int k = XkbKeycodeToKeysym(f->dpy, ev.xkey.keycode, 0, 0);
				for (unsigned int i = 0; i < 124; i += 2) {
					if (FENSTER_KEYCODES[i] == k) {
						f->inp.key[FENSTER_KEYCODES[i + 1]] = 1;
						f->inp.key_down[FENSTER_KEYCODES[i + 1]] = 1;
						break;
					}
				}
				f->inp.key_mod[0] = !!(m & ControlMask);
				f->inp.key_mod[1] = !!(m & ShiftMask);
				f->inp.key_mod[2] = !!(m & Mod1Mask);
				f->inp.key_mod[3] = !!(m & Mod4Mask);
				break;
			}
		case KeyRelease:
			{
				int m = ev.xkey.state;
				int k = XkbKeycodeToKeysym(f->dpy, ev.xkey.keycode, 0, 0);
				for (unsigned int i = 0; i < 124; i += 2) {
					if (FENSTER_KEYCODES[i] == k) {
						f->inp.key_down[FENSTER_KEYCODES[i + 1]] = 0;  // Only update keys
						break;
					}
				}
				f->inp.key_mod[0] = !!(m & ControlMask);
				f->inp.key_mod[1] = !!(m & ShiftMask);
				f->inp.key_mod[2] = !!(m & Mod1Mask);
				f->inp.key_mod[3] = !!(m & Mod4Mask);
				break;
			}
		}
	}
	return 0;
}

FENSTER_API void fenster_fullscreen(struct fenster* f, const int enabled) {
	XEvent xev = {0};
	xev.type = ClientMessage;
	xev.xclient.window = f->w;
	xev.xclient.message_type = wm_state;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = enabled ? 1 : 0;  // 1 = add, 0 = remove
	xev.xclient.data.l[1] = wm_fullscreen;
	xev.xclient.data.l[2] = 0;
	XSendEvent(f->dpy, DefaultRootWindow(f->dpy), False, SubstructureNotifyMask | SubstructureRedirectMask, &xev);
	XFlush(f->dpy);
}

#ifdef FENSTER_CURSOR
FENSTER_API void fenster_cursor(struct fenster* f, const int type) {
	if (type == current_cursor_type) return;
	if (!cursors_initialized) {
		cursors[0] = 0;                                           // None/hidden
		cursors[1] = XCreateFontCursor(f->dpy, XC_left_ptr);      // Normal arrow
		cursors[2] = XCreateFontCursor(f->dpy, XC_hand2);         // Pointer/hand
		cursors[3] = XCreateFontCursor(f->dpy, XC_watch);         // Progress
		cursors[4] = XCreateFontCursor(f->dpy, XC_crosshair);     // Crosshair
		cursors[5] = XCreateFontCursor(f->dpy, XC_xterm);         // Text
		cursors_initialized = 1;
	}

	if (type == 0) {
		// Hide cursor
		XColor dummy;
		Pixmap blank = XCreateBitmapFromData(f->dpy, f->w, "\0\0\0\0\0\0\0\0", 1, 1);
		Cursor invisible = XCreatePixmapCursor(f->dpy, blank, blank, &dummy, &dummy, 0, 0);
		XDefineCursor(f->dpy, f->w, invisible);
		XFreeCursor(f->dpy, invisible);
		XFreePixmap(f->dpy, blank);
	} else {
		// Set the cursor from our pre-created cursors
		XDefineCursor(f->dpy, f->w, cursors[type]);
	}

	current_cursor_type = type;
	XFlush(f->dpy);
}
#endif // FENSTER_CURSOR

FENSTER_API void fenster_resize(struct fenster* f, const int width, const int height) {
	if (f->allow_resize == 0) return;
	XResizeWindow(f->dpy, f->w, width, height);
	XFlush(f->dpy);
}

#endif // FENSTER_LINUX

#ifndef FENSTER_WIN

FENSTER_API void fenster_sleep(const int64_t ms) {
	usleep(ms * 1000);
/*
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000000;
	nanosleep(&ts, NULL);
*/
}

FENSTER_API int64_t fenster_time(void) {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
}

#endif // !FENSTER_WIN

FENSTER_API void fenster_sync(struct fenster* f, const int fps) {
	int64_t frame_time = 1000 / fps;
	int64_t elapsed = fenster_time() - f->last_sync;
	if (elapsed < frame_time) {
		fenster_sleep(frame_time - elapsed);
	}
	f->last_sync = fenster_time();
}

#endif // FENSTER_IMPLEMENTATION

#endif // FENSTER_H

