cmake_minimum_required(VERSION 3.10)
project(kllamabooks VERSION 0.1.2)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Network Svg DBus REQUIRED)
find_package(KF6Wallet REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLCIPHER REQUIRED sqlcipher)

add_executable(kllamabooks
    src/main.cpp
    src/OllamaClient.cpp
    src/OllamaClient.h
    src/OllamaModelInfo.h
    src/ModelExplorer.cpp
    src/ModelExplorer.h
    src/ModelSelectionDialog.cpp
    src/ModelSelectionDialog.h
    src/BookDatabase.cpp
    src/BookDatabase.h
    src/WalletManager.cpp
    src/WalletManager.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/DatabaseSettingsDialog.cpp
    src/DatabaseSettingsDialog.h
    src/ChatSettingsDialog.cpp
    src/ChatSettingsDialog.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/QueueManager.cpp
    src/QueueManager.h
    src/QueueWindow.cpp
    src/QueueWindow.h
    src/NotificationDelegate.cpp
    src/NotificationDelegate.h
    src/ChatInputWidget.cpp
    src/ChatInputWidget.h
    src/AiActionDialog.cpp
    src/AiActionDialog.h
    src/MergeDocumentsDialog.cpp
    src/MergeDocumentsDialog.h
    src/TemplateParser.cpp
    src/TemplateParser.h
    src/AIOperationsDialog.cpp
    src/AIOperationsDialog.h
    src/AIOperationsManager.cpp
    src/AIOperationsManager.h
    src/AIOperationsEditorWidget.cpp
    src/AIOperationsEditorWidget.h
    src/DocumentEditWindow.cpp
    src/DocumentEditWindow.h
    src/DocumentReviewDialog.cpp
    src/DocumentReviewDialog.h
    src/DocumentHistoryDialog.cpp
    src/DocumentHistoryDialog.h
    src/NewDocumentDialog.cpp
    src/NewDocumentDialog.h
    src/resources.qrc
)

find_package(KF6CoreAddons REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6I18n REQUIRED)

target_link_libraries(kllamabooks
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::DBus
    KF6::CoreAddons
    KF6::XmlGui
    KF6::ConfigWidgets
    KF6::I18n
    KF6::Wallet
    ${SQLCIPHER_LIBRARIES}
)

target_include_directories(kllamabooks PRIVATE ${SQLCIPHER_INCLUDE_DIRS})

target_compile_definitions(kllamabooks PRIVATE KGHN_APP_VERSION="${PROJECT_VERSION}")


# Installation
install(TARGETS kllamabooks
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

install(FILES src/kllamabooks.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    RENAME com.arran4.kllamabooks.desktop
)

install(FILES src/com.arran4.kllamabooks.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

# Tests
enable_testing()
find_package(Qt6 COMPONENTS Test REQUIRED)
add_executable(test_TemplateParser tests/test_TemplateParser.cpp src/TemplateParser.cpp)
target_link_libraries(test_TemplateParser Qt6::Test Qt6::Core)
add_test(NAME test_TemplateParser COMMAND test_TemplateParser)

install(FILES src/assets/icon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME kllamabooks.svg
)

install(FILES src/kllamabooks.notifyrc
    DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6
)

install(FILES src/kllamabooksui.rc
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/kxmlgui6/kllamabooks
)

install(FILES src/com.arran4.kllamabooks.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
