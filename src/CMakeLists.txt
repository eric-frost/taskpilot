# SPDX-FileCopyrightText: 2026 Eric Frost
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(taskpilot)

target_sources(taskpilot PRIVATE
    main.cpp
    mainwindow.cpp
    fetchers.cpp
    journaltab.cpp
    systemd/systemdmanager.cpp
    services/servicemodel.cpp
    services/servicestab.cpp
    services/unitactions.cpp
    util/rowtablemodel.cpp
    util/commandtab.cpp
    util/auth.cpp
    util/dialogs.cpp
    editors/cronedit.cpp
    editors/autostartedit.cpp
    editors/bindlnedit.cpp
    editors/unitcreator.cpp
)

target_include_directories(taskpilot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(taskpilot PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Concurrent
    KF6::AuthCore
    KF6::CoreAddons
    KF6::I18n
    KF6::XmlGui
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::ConfigWidgets
    KF6::Crash
    KF6::ItemModels
    KF6::ItemViews
    KF6::WidgetsAddons
)

install(TARGETS taskpilot ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Install the KXmlGui resource file.
install(FILES taskpilotui.rc DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/taskpilot)

# Privileged KAuth helper (runs as root via polkit/D-Bus).
add_executable(taskpilot_auth auth/taskpilot_auth.cpp)
target_include_directories(taskpilot_auth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(taskpilot_auth PRIVATE Qt6::Core KF6::AuthCore)
install(TARGETS taskpilot_auth DESTINATION ${KAUTH_HELPER_INSTALL_DIR})

kauth_install_helper_files(taskpilot_auth org.kde.taskpilot root)
kauth_install_actions(org.kde.taskpilot ${CMAKE_CURRENT_SOURCE_DIR}/auth/org.kde.taskpilot.actions)
