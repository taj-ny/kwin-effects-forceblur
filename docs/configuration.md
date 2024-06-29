# General
### Window opacity affects blur
Since Plasma 6, window opacity now affects blur opacity with no option to disable it in the stock blur effect.

Enabled (default):
![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/525a3611-62f0-4c7e-b01c-253a05cbd3ca)

Disabled:
![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/b4f35a24-e288-4c51-9707-494942abdaa0)

# Force blur
### Blur window decorations
Whether to blur window decorations, including borders. Enable this if your window decoration doesn't support blur or you want rounded corners.

This option will override the blur region specified by the decorations.

### Paint windows as non-opaque
Fixes transparency for some applications by marking their windows as opaque. This will only be done for force-blurred windows.

# Fake blur
> [!NOTE]  
> Fake blur doesn't support multiple screens on X11.

Fake blur is mainly intended for laptop users who want longer battery life.

If a window is entirely in front of the wallpaper, why keep blurring the same image over and over? That's a massive waste of resources.
The effect can blur the wallpaper only once, store it in memory and paint it behind windows instead of actually blurring them.
Because the window isn't transparent anymore, there's also no need to paint anything behind it.

### Use real blur for windows that are in front of other windows
When two windows overlap, you won't be able to see the window behind.
![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/e581b5c1-7b2c-41c4-b180-4da5306747e1)

If this option is enabled, the effect will switch to real blur when necessary.

https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/7bae6a16-6c78-4889-8df1-feb24005dabc

### Image source
The image to use for fake blur.

- Desktop wallpaper - A screenshot of the desktop is taken for every screen. This option is currently experimental. You may run into issues.
- Custom - The image is read from a file and scaled for every screen without respecting the aspect ratio. Supported formats are JPEG and PNG.

### Blur image
Whether to blur the image used for fake blur. This is only done once.
