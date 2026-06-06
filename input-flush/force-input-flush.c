#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

struct wl_proxy;
struct wl_display;

static void (*wl_proxy_marshal_)(struct wl_proxy *, uint32_t, ...);
static int (*wl_display_flush)(struct wl_display *);
static struct wl_display *(*wl_proxy_get_display)(struct wl_proxy *);
static const char *(*wl_proxy_get_class)(struct wl_proxy *);

__attribute__((visibility("hidden"), constructor))
void init(void) {
    wl_proxy_marshal_ = dlsym(RTLD_NEXT, "wl_proxy_marshal");
    wl_display_flush = dlsym(RTLD_NEXT, "wl_display_flush");
    wl_proxy_get_display = dlsym(RTLD_NEXT, "wl_proxy_get_display");
    wl_proxy_get_class = dlsym(RTLD_NEXT, "wl_proxy_get_class");
}

__attribute__((visibility("hidden"), noinline))
void maybe_flush(struct wl_proxy *proxy) {
    if (wl_proxy_get_class) {
        const char *cls = wl_proxy_get_class(proxy);
        if (cls
            && strcmp(cls, "zwlr_virtual_pointer_v1")
            && strcmp(cls, "zwp_virtual_keyboard_v1"))
            return;
    }
    if (wl_proxy_get_display) {
        struct wl_display *disp = wl_proxy_get_display(proxy);
        if (wl_display_flush && disp)
            wl_display_flush(disp);
    }
}

__attribute__((visibility("default")))
void wl_proxy_marshal(struct wl_proxy *proxy, __attribute__((unused)) uint32_t opcode, ...) {
#if !__has_builtin(__builtin_apply)
#error "GCC is required"
#else
    void *args = __builtin_apply_args();
    __builtin_apply((void (*)(void))wl_proxy_marshal_, args, 256); // sizeof(wl_argument)*WL_CLOSURE_MAX_ARGS = 160 < 256
    asm volatile("" ::: "memory");
#endif
    maybe_flush(proxy);
}
