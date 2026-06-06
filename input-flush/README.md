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

<table><caption>

`sudo bpftrace --unsafe input-latency.bt $(pidof vncagent-x11)`

</caption><thead><tr><th>Before</th><th>After</th></tr></thead><tbody><tr><td valign="top">

```
marshals: 446
flushes: 141
worst: 54204 us

latency for oldest event (us):
@latency_oldest_us:
[8K, 16K)              3 |@                                                   |
[16K, 32K)            14 |@@@@@                                               |
[32K, 64K)           124 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|


latency for newest event (us):
@latency_newest_us:
[1K, 2K)               1 |                                                    |
[2K, 4K)               9 |@@@@@@@@                                            |
[4K, 8K)              19 |@@@@@@@@@@@@@@@@@                                   |
[8K, 16K)             49 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@      |
[16K, 32K)            55 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[32K, 64K)             8 |@@@@@@@                                             |
```

</td><td valign="top">

```
marshals: 499
flushes: 499
worst: 3211 us

latency for oldest event (us):
@latency_oldest_us:
[2, 4)                 5 |@                                                   |
[4, 8)                34 |@@@@@@@                                             |
[8, 16)               82 |@@@@@@@@@@@@@@@@@                                   |
[16, 32)              65 |@@@@@@@@@@@@@                                       |
[32, 64)             246 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[64, 128)             36 |@@@@@@@                                             |
[128, 256)            12 |@@                                                  |
[256, 512)             1 |                                                    |
[512, 1K)              8 |@                                                   |
[1K, 2K)               8 |@                                                   |
[2K, 4K)               2 |                                                    |


latency for newest event (us):
@latency_newest_us:
[2, 4)                 5 |@                                                   |
[4, 8)                36 |@@@@@@@                                             |
[8, 16)               81 |@@@@@@@@@@@@@@@@                                    |
[16, 32)              69 |@@@@@@@@@@@@@@                                      |
[32, 64)             248 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[64, 128)             32 |@@@@@@                                              |
[128, 256)             9 |@                                                   |
[256, 512)             1 |                                                    |
[512, 1K)              8 |@                                                   |
[1K, 2K)               8 |@                                                   |
[2K, 4K)               2 |                                                    |
```

</td></tr></tbody></table>

### Notes

```
7ec2dc3b336e603e0c80d893e92a4ba6eea67347  /usr/bin/vncagent-x11
8cf7b82e1bfdfb280da78eeddba29d072602c13c  /usr/bin/vncagent-wlr
```

Some relevant functions:

- X11
  - sub_42a4b0: calls `XTestFakeMotionEvent` and `XTestFakeButtonEvent` for the pointer event ipc handler
- Wayland
  - sub_4222e0: pointer event ipc handler
    - sub_4245e0: pointer motion
  - sub_422330: screen update callback for ipc handler
    - sub_422d40: screen capture + wl_display_roundtrip
      - sub_423e90: zwlr_screencopy capture_output

Difference between pointer events handling:

- X11
  - `XTestFakeMotionEvent` is synchronous.
  - Main agent event loop handles X11 stuff, so it gets flushed.
  - Screen capture is based on XDAMAGE events, or a 20 Hz fallback polling rate.
- Wayland
  - `zwlr_virtual_pointer` just marshals it into the output buffer.
  - Main agent event loop does not do anything with Wayland's event loop (it doesn't use `wl_display_get_fd`).
  - Nothing in the pointer input IPC handler ever calls `wl_display_flush`.
  - The only regular flush that happens is part of the `wl_display_roundtrip` for the screen capture (which blocks until it completes).
  - Screen capture is run at a 50 ms interval, effectively 16 Hz: `sub_43d110(..., "_PollIntervalWlr", ..., 0x32, ...)`.

```
sudo bpftrace --unsafe input-latency.bt $(pidof vncagent-x11)
```
