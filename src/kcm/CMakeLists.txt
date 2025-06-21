set(kwin_better_blur_config_SRCS
    blur_config.cpp
    blur_config.h
    blur_config.qrc
)

ki18n_wrap_ui(kwin_better_blur_config_SRCS blur_config.ui)
kconfig_add_kcfg_files(kwin_better_blur_config_SRCS ../blurconfig.kcfgc)

# This will only work as long as KWin and KWin X11 have the same interface
qt_add_dbus_interface(kwin_better_blur_config_SRCS ${KWIN_EFFECTS_INTERFACE} kwineffects_interface)

add_library(kwin_better_blur_config MODULE ${kwin_better_blur_config_SRCS})
target_link_libraries(kwin_better_blur_config
    KF6::KCMUtils
    KF6::CoreAddons
    KF6::I18n
    Qt6::DBus
)

if(BETTERBLUR_WAYLAND)
    install(TARGETS kwin_better_blur_config DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/configs)
endif()
if(BETTERBLUR_X11)
    install(TARGETS kwin_better_blur_config DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin-x11/effects/configs)
endif()

add_compile_definitions(ABOUT_VERSION_STRING="${PROJECT_VERSION}")
