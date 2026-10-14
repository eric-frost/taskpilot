# SPDX-FileCopyrightText: 2026 Eric Frost
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(taskpilot VERSION 0.4.0 LANGUAGES CXX)

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

# 2. .deb and .rpm packaging (cpack -G DEB|RPM in the build dir).  Build each
#    package on the distro it targets so library dependencies are detected
#    from that distro; packaging/build-packages.sh does this in containers.
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "taskpilot")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "System task manager for systemd, cron, mounts, ports and containers")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/eric-frost/taskpilot")
set(CPACK_PACKAGE_VENDOR "Eric Frost")
# Pseudonymous maintainer — never the real name/email.
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Eric Frost <9555559+eric-frost@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)        # auto-detect Qt6/KF6 library deps
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)        # taskpilot_<ver>_<arch>.deb
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "System/Monitoring")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_DESCRIPTION "TaskPilot is a KDE application for managing systemd services and timers, cron jobs, autostart entries, listening ports, the journal, snaps, VMs and containers, mounts and login sessions.")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)              # auto-detect Qt6/KF6 library deps
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)         # e.g. taskpilot-<ver>-1.fc44.x86_64.rpm
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
# Don't claim shared system directories, which other packages own.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications /usr/share/metainfo /usr/share/icons
    /usr/share/icons/hicolor /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps /usr/share/kxmlgui5
    /usr/share/dbus-1 /usr/share/dbus-1/system.d /usr/share/dbus-1/system-services
    /usr/share/polkit-1 /usr/share/polkit-1/actions
    /usr/lib64/libexec /usr/lib64/libexec/kf6 /usr/lib64/libexec/kf6/kauth
    /usr/libexec/kf6 /usr/libexec/kf6/kauth /usr/lib/libexec /usr/lib/libexec/kf6
    /usr/lib/libexec/kf6/kauth)
include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
