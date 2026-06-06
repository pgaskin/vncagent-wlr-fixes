# Input flush bug

RealVNC 7.17.0's `vncagent-wlr` has a bug causing bursty mouse/keyboard events and jittery mouse movements.

The root cause is that `wl_display_flush` is only getting called every 60ms by the screencopy roundtrip in the screen polling loop, and not after each input event.

This can be fixed by intercepting `wl_proxy_marshal` and calling `wl_display_flush` afterwards if it's a message for `zwlr_virtual_pointer_v1` or
`zwp_virtual_keyboard_v1`. This method also has the advantage of not requiring fixed offsets. We also don't need to worry about thread safety.

The easiest and least-intrusive way to do this is with a `LD_PRELOAD` shim. Using ptrace would be more complicated since we would need to look for the vncagent process, and then we'd need to attach to it, add breakpoints, catch them, and replace instructions while single-stepping. Using bpf uprobes isn't really an option since it's not designed for calling back in to userspace.

Implementing it is a little bit complicated since `wl_proxy_marshal` is variadic, so we need to preserve the stack and registers, and the binaries are also compiled with CET, so we can't just replace the return address in the stack.

GCC has `__builtin_apply` for doing this kind of shimming.

```bash
gcc -Wall -Wextra -shared -fPIC -O2 -o force-input-flush.so force-input-flush.c -ldl
```

```bash
LD_PRELOAD=/path/to/force-input-flush.so vncserver-x11
```

### Analysis

TODO: document
