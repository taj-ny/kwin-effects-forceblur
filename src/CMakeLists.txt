add_subdirectory(kcm)

set(forceblur_SOURCES
    blur.cpp
    blur.qrc
    main.cpp
)

kconfig_add_kcfg_files(forceblur_SOURCES
    blurconfig.kcfgc
)

add_library(forceblur MODULE ${forceblur_SOURCES})
target_link_libraries(forceblur PRIVATE
    KWin::kwin

    KF6::ConfigGui
)
if (${KDecoration3_FOUND})
    target_link_libraries(forceblur PRIVATE KDecoration3::KDecoration)
else()
    target_link_libraries(forceblur PRIVATE KDecoration2::KDecoration)
endif()

install(TARGETS forceblur DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
