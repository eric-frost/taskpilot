# SPDX-FileCopyrightText: 2026 Eric Frost
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(taskpilot VERSION 0.3.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.5.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMInstallIcons)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Auth
    CoreAddons
    I18n
    XmlGui
    Config
    ConfigWidgets
    Crash
    ItemModels
    ItemViews
    WidgetsAddons
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src)

# 1. Desktop entry + AppStream metadata + scalable app icon
install(FILES io.github.ericfrost.taskpilot.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES io.github.ericfrost.taskpilot.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES io.github.ericfrost.taskpilot.svg
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)

# 2. .deb packaging (cpack -G DEB in the build dir).
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "taskpilot")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "System task manager for systemd, cron, mounts, ports and containers")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/eric-frost/taskpilot")
# Pseudonymous maintainer — never the real name/email.
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Eric Frost <9555559+eric-frost@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)        # auto-detect Qt6/KF6 library deps
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)        # taskpilot_<ver>_<arch>.deb
include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
