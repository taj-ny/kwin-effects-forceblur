# Simple
A simple configuration system with a UI. For more complex configuration (for example blur menus but not firefox's menus), use window rules.

Obvious options are not explained here.

## General

| Option                          | Description                                                                                                                                                                                                                                                                                                                                                                                                                      |
|---------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Noise strength**              | Noise is used to mask color banding.                                                                                                                                                                                                                                                                                                                                                                                             |
| **Window opacity affects blur** | Whether the blur opacity should be affected by the window's opacity. Window fading animations will still affect the blur opacity.<br><br>Enabled:<br><img src="https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/525a3611-62f0-4c7e-b01c-253a05cbd3ca" width="600"><br>Disabled:<br><img src="https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/b4f35a24-e288-4c51-9707-494942abdaa0" width="600"> |

## Force blur
| Option                              | Description                                                                                                                                                                                                                                                                  |
|-------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Window classes**                  | A list of classes of windows to force blur, separated by new line.<br><br>Regular expressions are supported and can be specified by using the ``$regex:`` prefix. Example: ``$regex:org\.kde\..*``                                                                           |
| **Blur window decorations as well** | Whether to blur the window decorations (titlebar and borders) in addition to the content. This will override the decoration's blur region, possibly breaking rounded corners, in which case you need to manually specify the corner radius in the *Rounded corners* section. |
| **Treat windows as transparent**    | Whether force-blurred windows should be considered as transparent in order to fix transparency for applications that don't mark their windows as transparent. See #38 for an example.                                                                                        |

## Rounded corners
| Option                       | Description                                                                            |
|------------------------------|----------------------------------------------------------------------------------------|
| **Window top corner radius** | Requires *Blur window decorations as well* to be enabled for windows with decorations. |
| **Anti-aliasing**            | Makes rounded corners appear smoother.                                                 |

## Static blur
When enabled, a cached texture will be painted behind the window instead of actually blurring the background (dynamic blur).
The blurred areas of the window will also be marked as opaque, causing KWin to not paint anything behind the window.
Both things result in lower resource usage.

Static blur is mainly intended for laptop users who want longer battery life while still having blur everywhere.

| Option                                                      | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
|-------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Use dynamic blur for windows with another window behind** | Whether to switch to dynamic blur when a window has another window behind it, allowing you to see the one behind.<br><br>Enabled:<br><img src="https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/b4f35a24-e288-4c51-9707-494942abdaa0" width="600"><br>Disabled:<br><img src="https://github.com/taj-ny/kwin-effects-forceblur/assets/79316397/e581b5c1-7b2c-41c4-b180-4da5306747e1" width="600">                                                   |
| **Texture source**                                          | The image to use for static blur.<br>- Desktop wallpaper - A screenshot of the desktop is taken for every screen. Icons and widgets will be included. The cached texture is invalidated when the entire desktop is repainted, which can happen when the wallpaper changes, icons are interacted with or when widgets update.<br>- Custom - The specified image is scaled for every screen without respecting the aspect ratio. Supported formats are JPEG and PNG. |
| **Blur texture**                                            | Whether to blur the texture. This is only done once during texture creation.                                                                                                                                                                                                                                                                                                                                                                                       |

## Window rules

| Option                                    | Description                                                                                                                                                                   |
|-------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Convert simple config to window rules** | Whether to automatically convert simple configuration into window rules. Disabling this will cause most options in the simple configuration UI to not have an effect anymore. |

# Window rules
Configuration UI for window rules will be added in v2. The configuration file is located at ``~/.config/kwinbetterblurrc`` and isn't created automatically.

Simple configuration is converted into multiple rules with priorities lower than 0. They are not added into the configuration file. This behavior can be disabled in the configuration UI in the *Window Rules* tab.

An example is provided at the end.

## File structure
### Window rule
**Group**: [WindowRules][\$RuleName], where **$RuleName (string)** is the unique name of this rule.

Rules are not evaluated in order as they appear in the file.

| Property     | Type          | Description                                                                                                                             |
|--------------|---------------|-----------------------------------------------------------------------------------------------------------------------------------------|
| **Priority** | ``int (>=0)`` | Properties of rules with higher priority override properties of rules with lower priority.<br><br>Priorities lower than 0 are reserved. |

### Conditions
**Group**: [WindowRules][\$RuleName][Conditions][\$ConditionName], where **$ConditionName (string)** is the unique name of this condition in the rule.

At least 1 (or 0 if none specified) condition must be satisfied in order for this rule to be applied. All specified subconditions in a condition must be satisfied.

| Property                    | Type                                                                                             | Description                                                                                                                                                                                                                                                                                   |
|-----------------------------|--------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Negate**                  | ``enum (WindowClass, WindowState, WindowType)``<br>Multiple values are allowed.                  | Which subconditions should be negated.<br><br>Separated by space.                                                                                                                                                                                                                             |
| **HasWindowBehind**         | ``bool``                                                                                         | Whether only windows that (don't) have another window (excluding desktops) behind them should satisfy this condition.                                                                                                                                                                         |
| **Internal**                | ``bool``                                                                                         | Internal KWin windows.                                                                                                                                                                                                                                                                        |
| **WindowClass**             | ``string`` or ``regex``                                                                          | A regular expression executed on the window class. Only a partial match is required.<br><br>For an exact match, use ``^class$``.<br>To specify multiple classes, separate them by ``\|``: ``class1\|class2``.<br>A dot matches any character, which can be prevented by escaping it: ``\\.``. |
| **WindowState**             | ``enum (Fullscreen, Maximized)``<br>Multiple values are allowed.                                 | Separated by space.                                                                                                                                                                                                                                                                           |
| **WindowType**              | ``enum (Dialog, Dock, Normal, Menu, Toolbar, Tooltip, Utility)``<br>Multiple values are allowed. | Separated by space.                                                                                                                                                                                                                                                                           |

### Properties
**Group**: [WindowRules][\$RuleName][Properties]

Properties don't have default values. Unset properties don't override properties of other rules.

| Property                     | Type             | Description                                                                                                                                                                                                                                                                                                                                                                |
|:-----------------------------|------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **BlurContent**              | ``bool``         | Whether to force blur the window's content, excluding decorations.                                                                                                                                                                                                                                                                                                         |
| **BlurDecorations**          | ``bool``         | See *Blur window decorations as well* in the *Simple* section. The difference is that in window rules it's possible to blur only the decorations and the corner radius is specified in the *CornerRadius* property.                                                                                                                                                        |
| **BlurStrength**             | ``int (1 - 15)`` |                                                                                                                                                                                                                                                                                                                                                                            |
| **CornerRadius**             | ``float (>= 0)`` | Corner radius for the blur region.<br><br>A single number specifies the same radius for both top and bottom corners. Example: ``12.5``<br>Two numbers separated by a space specify different radii for top and bottom corners respectively. Example: ``0 12.5``<br><br>Top corner radius will only be applied to windows with force-blurred decorations or no decorations. |
| **CornerAntialiasing**       | ``float (>= 0)`` | Makes rounded corners appear smoother. Recommended value is 1.                                                                                                                                                                                                                                                                                                             |
| **ForceTransparency**        | ``bool``         | See *Treat windows as transparent* in the *Simple* section.                                                                                                                                                                                                                                                                                                                |
| **StaticBlur**               | ``bool``         | Whether to use static blur instead of dynamic blur for the window.                                                                                                                                                                                                                                                                                                         |
| **WindowOpacityAffectsBlur** | ``bool``         | See *Window opacity affects blur* in the *Simple* section.                                                                                                                                                                                                                                                                                                                 |

## Example
```
[WindowRules][AllWindows][Properties]
CornerAntialiasing = true
WindowOpacityAffectsBlur = false


[WindowRules][ForceBlur][Conditions][0]
WindowClass = clementine|kate|kwrite|org\\.freedesktop\\.impl\\.portal\\.desktop\\.kde|plasmashell
WindowType = Dialog Normal Menu Toolbar Tooltip Utility

[WindowRules][ForceBlur][Conditions][1]
WindowClass = firefox
WindowType = Normal

[WindowRules][ForceBlur][Properties]
BlurContent = true
BlurDecorations = true


[WindowRules][StaticBlur][Conditions][0]
HasWindowBehind = false

[WindowRules][StaticBlur][Properties]
StaticBlur = true


[WindowRules][WindowCorners][Conditions][0]
Negate = WindowState
WindowState = Fullscreen Maximized
WindowType = Normal

[WindowRules][WindowCorners][Properties]
CornerRadius = 15


[WindowRules][MenuCorners][Conditions][0]
WindowType = Menu

[WindowRules][MenuCorners][Properties]
CornerRadius = 10
```