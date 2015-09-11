# touchpoint
Trackpoint emulator using evdev multitouch touchpad device.

# Usage
Start with root permissions, or add user to group owning `/dev/input/event*` files.

```bash
ls -l /dev/input
...
crw-rw---- 1 root input 13, 70 wrz 11 08:56 event6
...

sudo usermod -a -G input {{user}}
```

# Work in progress
Device location is hardcoded for now, only basic functionality without ability to config.

# Requires
`libevdev-dev`  
`libxdo-dev`
