add_subdirectory(kcm)

set(blurplus_blur_SOURCES
    blur.cpp
    blur.qrc
    main.cpp
)

kconfig_add_kcfg_files(blurplus_blur_SOURCES
    blurconfig.kcfgc
)

add_library(blurplus_blur MODULE ${blurplus_blur_SOURCES})
target_link_libraries(blurplus_blur PRIVATE
    KWin::kwin

    KF6::ConfigGui

    KDecoration2::KDecoration
)

install(TARGETS blurplus_blur DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
