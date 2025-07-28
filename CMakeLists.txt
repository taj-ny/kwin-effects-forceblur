cmake_minimum_required(VERSION 3.16.0)

project(kwin_better_blur)
set(PROJECT_VERSION "1.5.0")
set(PROJECT_VERSION_MAJOR 0)

set(KF_MIN_VERSION "5.240.0")
set(QT_MIN_VERSION "6.6.0")
set(QT_MAJOR_VERSION 6)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CPACK_PACKAGE_NAME "kwin-better-blur")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_FILE_NAME "kwin-better-blur")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fork of the KWin Blur effect for KDE Plasma 6 with additional features (including force blur) and bug fixes")
include(CPack)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DQT_NO_DEBUG_OUTPUT")

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

option(BETTERBLUR_WAYLAND "Whether to build Better Blur for Wayland if present." ON)
option(BETTERBLUR_X11 "Whether to build Better Blur for X11 if present." ON)

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Gui
    Core
    DBus
    UiTools
    Widgets
    OpenGL
    Network
    Xml
)

include_directories(${Qt6Widgets_INCLUDE_DIRS} ${Qt6Network_INCLUDE_DIRS} ${Qt6OpenGL_INCLUDE_DIRS} ${Qt6Xml_INCLUDE_DIRS})
add_definitions(${Qt6Widgets_DEFINITIONS})

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config
    ConfigWidgets
    CoreAddons
    Crash
    GlobalAccel
    I18n
    KIO
    Service
    #Init
    Notifications
    Service
    WidgetsAddons
    WindowSystem
    GuiAddons
    KCMUtils
)

find_package(epoxy REQUIRED)

find_package(X11 REQUIRED)
find_package(XCB REQUIRED COMPONENTS XCB)

if(BETTERBLUR_WAYLAND)
    find_package(KWin COMPONENTS
        kwineffects
    )
    if (${KWin_FOUND})
        find_package(KWinDBusInterface CONFIG REQUIRED)
    else()
        set(BETTERBLUR_WAYLAND OFF)
    endif()
endif()
if(BETTERBLUR_X11)
    find_package(KWinX11 COMPONENTS
        kwineffects
    )
    if (${KWinX11_FOUND})
        find_package(KWinX11DBusInterface CONFIG REQUIRED)
        set(KWin_VERSION ${KWinX11_VERSION})
    else()
        set(BETTERBLUR_X11 OFF)
    endif()
endif()

if(${KWin_VERSION} VERSION_LESS 6.4)
    message(FATAL_ERROR "Better Blur does not support your Plasma version (${KWin_VERSION}). See the README for more information.")
endif()

find_package(KDecoration3 REQUIRED)

add_subdirectory(src)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
