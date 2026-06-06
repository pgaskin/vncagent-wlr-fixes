# vlcagent-wlr-fixes

RealVNC Server 7.17.0 introduced hidden support for Wayland compositors. It generally works well, but there are some compositor-specific and some implementation issues.

This is nice since their proprietary UDP-based RTP/SCTP-wrapped RFB protocol performs much better than wayvnc, especially over non-local networks. I did spend some time a while ago reversing the protocol and was able to figure out everything but the handshake and a few encodings, but the custom handshake for their proprietary auth mechanism makes it annoying to implement (even the unencrypted one still signs packets using HMAC with a negotiated key).

Last year, I considered writing a fake X11 server which implements just enough to run the server and proxy stuff to a Wayland compositor, but I didn't think it was worth the time compared to just sticking to X11 window managers.

Now that they have basic Wayland support, I figured shimming or patching the bugs or compatibility issues would be the best way forward.

You may also be interested in my [fixes and patches](https://github.com/pgaskin/vncpatch) for the RealVNC Android app, including an invisible menu (instead of the gigantic floating toolbar which gets in the way), dark mode support, key repeat support, and a fix for clicks randomly not working on high-frequency touchscreens (e.g., on a Pixel 9 or later).

If anyone from RealVNC is reading this:

- You should implement an additional `vncagent-xdg` variant using [xdg-desktop-portal](https://flatpak.github.io/xdg-desktop-portal/) for KDE and GNOME support.
- You should switch to [`ext_image_copy_capture_v1`](https://wayland.app/protocols/ext-image-copy-capture-v1) as the primary capture mode, only falling back to screencopy if it isn't supported. This has a better API and will also let you support client-side cursors.
- You need to make `vncserver-x11-core` forward clipboard events to the agent if it's `vncagent-wlr`. Xwayland clipboard syncing doesn't really work and needs separate tools.
- You need to flush after sending input events (or anything, really). This isn't expensive, and the reason you didn't have to do it for X11 is because `XTEST` does it implicitly for each event.
- Multi-monitor support shouldn't be too hard (it's easier than X11), so I assume you're already working on it.
- I love RealVNC and strongly recommend it to people!!!

### TLDR

For 7.17.0, you'll need:

- A Wayland compositor supporting:
  - `zwlr_screencopy_manager_v1`
  - `zwlr_virtual_pointer_v1`
  - `zwp_virtual_keyboard_v1`
  - `zwlr_data_control_manager_v1`
- If your compositor is now sway (especially if it's Smithay-based), you'll want [wl-uinput-proxy](https://github.com/pgaskin/wl-uinput-proxy) to fix the virtual pointer/keyboard.
  - If you use this, you need write access to `/dev/uinput`.
- Bubblewrap.

This will work well for basic single-monitor usage, except for client-to-server clipboard and missing client-side cursor support.

You can start the server with:

```bash
bwrap \
  --bind / / \
  --dev-bind /dev/uinput /dev/uinput \
  --bind /usr/bin/vncagent-wlr /usr/bin/vncagent-x11 \
  /usr/local/bin/wl-uinput-proxy \
  /usr/bin/vncserver-x11
```

Or as a systemd user unit:

```ini
[Unit]
PartOf=graphical-session.target
After=graphical-session.target
Requisite=graphical-session.target

[Service]
ExecStart=/usr/bin/bwrap --bind / / --dev-bind /dev/uinput /dev/uinput --bind /usr/bin/vncagent-wlr /usr/bin/vncagent-x11 /usr/local/bin/wl-uinput-proxy /usr/bin/vncserver-x11
Restart=always

RestartSec=10s
RestartSteps=4
RestartMaxDelaySec=160s

[Install]
WantedBy=niri.service
```

### Fixes

#### Virtual keyboard/mouse on compositors which have broken/missing support

I wrote a Wayland proxy which implements the `zwlr_virtual_pointer_v1` and `zwp_virtual_keyboard_v1` protocols using uinput. It works for other things too, not just RealVNC.

This works pretty well, with the only real limitation being that it emulates a system input device, so it can't be bound to a specific Wayland seat, and it needs permission to write to `/dev/uinput`.

See [wl-uinput-proxy](https://github.com/pgaskin/wl-uinput-proxy).

#### Bursty mouse/keyboard events and jittery mouse movements

TODO: document my research and testing for this

TODO: write a shim or patch to fix it by always flushing after input events

### Status of vncagent-wlr

Based on testing and static anaysis.

#### 7.17.0 (May 2026)

The GUI runs in Xwayland, `vncagent-wlr` is native.

It doesn't detect Wayland automatically, so you need to replace `vncagent-x11` with `vncagent-wlr`.

The following Wayland protocols are used:

- core
  - `wl_shm`, assuming the compositor implements the protocols correctly (e.g., sway)
  - `wl_shm_pool`
  - `wl_buffer`
  - `wl_seat`
  - `wl_output`
- screen capture
  - `zwlr_screencopy_manager_v1`
  - `zwlr_screencopy_frame_v1`
- input
  - `zwlr_virtual_pointer_manager_v1`
  - `zwlr_virtual_pointer_v1`
  - `zwp_virtual_keyboard_manager_v1`
  - `zwp_virtual_keyboard_v1`
- clipboard
  - `zwlr_data_control_manager_v1`
  - `zwlr_data_control_device_v1`
  - `zwlr_data_control_source_v1`
  - `zwlr_data_control_offer_v1`

Things that work:

- The user-mode server.
- The UI, including config and connection notifications.
- Basic single-display usage, assuming the compositor implements the protocols correctly (e.g., sway).
  - Mouse (absolute).
  - Keyboard.
  - Screen capture.
- The server-to-client clipboard.
- Running in a nested compositor (provided that DISPLAY and WAYLAND_DISPLAY are set correctly).
- Pausing for idle clients.

Thinks that don't:

- Multi-monitor doesn't work at all.
  - It only displays one monitor.
  - Pointer input is mapped across all monitors, making it misaligned.
- The client-to-server clipboard (i.e., pasting) doesn't work.
  - Based on some preliminary static analysis, I think this is because `vncserverui` (which is running in Xwayland) gets the IPC messages since `vncserver-x11-core` seems to be hardcoded to send them there (this makes sense given how system-mode VNC works on X11). The agent has an internal `EnableClipboard` option to have it handle the clipboard itself, which makes server-to-client work, but the client-to-server IPC messages still don't get delivered to the agent.
- Client-side cursor doesn't work.
  - This would require `vncagent-wlr` to support `ext_image_copy_capture_cursor_session_v1` (which also lacks wide compositor support).
- Damage tracking isn't used.
  - The screen is polled with a fixed interval of 60ms (~16.6 Hz)
  - For comparison, `vncagent-x11` polls at 20Hz is XDAMAGE is not used.
  - The overhead isn't too bad though, and it performs well overall, so this is mostly a non-issue.
- The system-mode and virtual-mode servers obviously don't work.
- The tray icon doesn't work (but who needs that anyways).
- The cursor is jittery, and keyboard/mouse events arrive in bursts (see my notes in the section about the virtual input flush fix above).

Compositor compatibility issues:

- Smithay-based compositors (e.g., niri) have broken virtual keyboard support (niri-wm/niri#403, smithay/smithay#1903). The keymap handling is wrong, and compositor keybinds don't work.
- GNOME and KDE use `libei` via `xdg-desktop-portal` and also don't implement screencopy.
- The `ext_image_copy_capture_v1` is intended to replace `screencopy`

I didn't test the proprietary extensions like file transfer, audio, chat, relative mouse position, and audio since I have other solutions for that on machines I need it for (e.g., [spicy-kvm](https://github.com/pgaskin/spicy-kvm)).

I also didn't test advanced features like input blocking and screen blanking.
