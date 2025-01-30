cmake_minimum_required(VERSION 3.16.0)

project(kwin_better_blur)
set(PROJECT_VERSION "1.4.0")
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

find_package(KWin REQUIRED COMPONENTS
    kwineffects
)

if(${KWin_VERSION} VERSION_GREATER_EQUAL 6.0.90)
    add_compile_definitions(KWIN_6_1_OR_GREATER)
endif()
if(${KWin_VERSION} VERSION_GREATER_EQUAL 6.1.90)
    add_compile_definitions(KWIN_6_2_OR_GREATER)
endif()

find_package(KDecoration2)
find_package(KDecoration3)
if (${KDecoration3_FOUND})
    add_compile_definitions(KDECORATION3)
endif()
if(NOT ${KDecoration3_FOUND} AND NOT ${KDecoration2_FOUND})
    message(FATAL_ERROR "Could not find KDecoration2 or KDecoration3.")
endif()


find_package(KWinDBusInterface CONFIG REQUIRED)

add_subdirectory(src)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
