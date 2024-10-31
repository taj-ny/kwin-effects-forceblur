#!/bin/sh

# Launches a nested Plasma X11 or Wayland session with virtual screens.
# Usage: ./plasma-nested.sh [x11/wayland]

width=1920
height=1080

dir=/tmp/kwin-better-blur
if [ ! -d $dir ]; then
    mkdir -p $dir/.local/share
    cp -r ~/.config $dir
    cp -r ~/.local/share/konsole/ $dir/.local/share

    # Tiling, no window decorations and custom shortcuts make it much harder to test
    rm -f $dir/.config/kwinrc $dir/.config/breezerc
fi

if [ "$1" = "x11" ]; then
    screen1="960/0x540/0+0+0 default"
    screen2="960/0x500/0+960+40 none"
    screen3="1500/0x540/0+210+540 none"

    unset XDG_SESSION_TYPE
    unset WAYLAND_DISPLAY

    Xephyr -screen ${width}x${height} +extension randr +extension glx +xinerama +extension render +extension damage +extension xvideo +extension composite -ac :9 &

    launch_plasma_command="dbus-run-session /bin/sh -c \"
    DISPLAY=:9
    HOME=$dir
    kwin_x11 &
    sleep 1
    xrandr --setmonitor A $screen1
    xrandr --setmonitor B $screen2
    xrandr --setmonitor C $screen3
    plasmashell > /dev/null 2>&1
    \""
elif [ "$1" == "wayland" ]; then
    # TODO Use $width and $height
    launch_plasma_command="
        HOME=$dir
        dbus-run-session kwin_wayland --output-count 2 --width 960 --height 960 -- /bin/sh -c \"plasmashell\" > /dev/null 2>&1"
fi

if [ -f /etc/machine-id ]; then
    # NixOS
    nix shell . --command /bin/sh -c "$launch_plasma_command"
else
    eval "$launch_plasma_command"
fi