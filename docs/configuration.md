# General
### Window opacity affects blur
Since Plasma 6, window opacity now affects blur opacity with no option to disable it in the stock blur effect.

Enabled (default):
![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/525a3611-62f0-4c7e-b01c-253a05cbd3ca)

Disabled:
![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/b4f35a24-e288-4c51-9707-494942abdaa0)

# Force blur
### Blur window decorations
Whether to blur window decorations, including borders. Enable this if your window decoration doesn't support blur, or you want rounded top corners.

This option will override the blur region specified by the decoration.

# Static blur
When enabled, the blur texture will be cached and reused. The blurred areas of the window will be marked as opaque, resulting in KWin not painting anything behind them.
Only one image per screen is cached at a time.

Static blur is mainly intended for laptop users who want longer battery life while still having blur everywhere.

### Use real blur for windows that are in front of other windows
By default, when two windows overlap, you won't be able to see the window behind.
![image](https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/e581b5c1-7b2c-41c4-b180-4da5306747e1)

If this option is enabled, the effect will automatically switch to real blur when necessary. At very high blur strengths, there may be a slight difference in the texture.

https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/7bae6a16-6c78-4889-8df1-feb24005dabc

### Image source
The image to use for static blur.

- Desktop wallpaper - A screenshot of the desktop is taken for every screen. Icons and widgets will be included. The cached texture is invalidated when the entire desktop is repainted,
which can happen when the wallpaper changes, icons are interacted with or when widgets update.
- Custom - The specified image is scaled for every screen without respecting the aspect ratio. Supported formats are JPEG and PNG.

### Blur image
Whether to blur the image used for static blur. This is only done once.
