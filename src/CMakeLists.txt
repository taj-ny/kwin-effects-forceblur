add_subdirectory(kcm)

function(replace_shader_include input output)
    file(READ shaders/roundedcorners.glsl ROUNDEDCORNERS_SHADER)
    file(READ "${input}" SHADER)
    string(REPLACE "#include \"roundedcorners.glsl\"" "${ROUNDEDCORNERS_SHADER}" SHADER "${SHADER}")
    file(WRITE "${output}" "${SHADER}")
endfunction()

replace_shader_include(shaders/texture.glsl shaders/texture.frag)
replace_shader_include(shaders/texture_core.glsl shaders/texture_core.frag)
replace_shader_include(shaders/upsample.glsl shaders/upsample.frag)
replace_shader_include(shaders/upsample_core.glsl shaders/upsample_core.frag)

set(forceblur_SOURCES
    blur.cpp
    blur.qrc
    main.cpp
    settings.cpp
)

kconfig_add_kcfg_files(forceblur_SOURCES
    blurconfig.kcfgc
)

if(BETTERBLUR_WAYLAND)
    add_library(forceblur MODULE ${forceblur_SOURCES})
    target_link_libraries(forceblur PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWin::kwin
    )
    install(TARGETS forceblur DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin/effects/plugins)
endif()
if(BETTERBLUR_X11)
    add_library(forceblur_x11 MODULE ${forceblur_SOURCES})
    target_link_libraries(forceblur_x11 PRIVATE
        KDecoration3::KDecoration
        KF6::ConfigGui
        KWinX11::kwin
    )
    target_compile_definitions(forceblur_x11 PRIVATE BETTERBLUR_X11)
    install(TARGETS forceblur_x11 DESTINATION ${KDE_INSTALL_PLUGINDIR}/kwin-x11/effects/plugins)
endif()
