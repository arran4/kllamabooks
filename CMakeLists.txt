cmake_minimum_required(VERSION 3.10)
project(kllamabooks VERSION 0.1.2)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Network Svg DBus Sql REQUIRED)
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
    src/db/Database.cpp
    src/db/Database.h
    src/db/Transaction.h
    src/db/Migrations.cpp
    src/db/Migrations.h
    src/db/MigrationFactory.cpp
    src/db/MigrationFactory.h
    src/store/ArtifactStore.cpp
    src/store/ArtifactStore.h
    src/store/FolderStore.cpp
    src/store/FolderStore.h
    src/store/ItemRef.h
    src/store/RecoveryStore.cpp
    src/store/RecoveryStore.h
    src/WalletManager.cpp
    src/WalletManager.h
    src/CredentialStore.cpp
    src/CredentialStore.h
    src/ConnectionMigration.cpp
    src/ConnectionMigration.h
    src/AppCredentialManager.cpp
    src/AppCredentialManager.h
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
    src/DocumentTemplatesManager.cpp
    src/DocumentTemplatesManager.h
    src/DocumentTemplatesEditorWidget.cpp
    src/DocumentTemplatesEditorWidget.h
    src/DocumentEditWindow.cpp
    src/DraftSelectionDialog.cpp
    src/DocumentEditWindow.h
    src/DraftSelectionDialog.h
    src/DocumentReviewDialog.cpp
    src/DocumentReviewDialog.h
    src/DocumentHistoryDialog.cpp
    src/DocumentHistoryDialog.h
    src/PromptHistoryDialog.cpp
    src/PromptHistoryDialog.h
    src/NewDocumentDialog.cpp
    src/NewDocumentDialog.h
    src/PasswordDialog.h
    src/PasswordDialog.cpp
    src/store/PromptStore.cpp
    src/store/PromptStore.h
    src/store/RunStore.cpp
    src/store/RunStore.h
    src/store/ChatStore.cpp
    src/store/ChatStore.h
    src/store/ActivityStore.cpp
    src/store/ActivityStore.h
    src/store/SearchStore.cpp
    src/store/SearchStore.h
    src/controllers/BookTreeController.cpp
    src/controllers/BookTreeController.h
    src/RecoveryBrowserDialog.cpp
    src/RecoveryBrowserDialog.h
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

install(FILES src/assets/icon.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
    RENAME kllamabooks.png
)

install(FILES src/kllamabooks.notifyrc
    DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6
)

install(FILES src/kllamabooksui.rc
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/kxmlgui6/kllamabooks
)

install(FILES src/kdocumenteditwindowui.rc
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/kxmlgui6/kllamabooks
)

install(FILES src/com.arran4.kllamabooks.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

# Tests
option(BUILD_TESTING "Build test targets" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 COMPONENTS Test Sql REQUIRED)
    add_executable(test_TemplateParser tests/test_TemplateParser.cpp src/TemplateParser.cpp)
    target_link_libraries(test_TemplateParser Qt6::Test Qt6::Core)
    add_test(NAME test_TemplateParser COMMAND test_TemplateParser)

    add_executable(test_BookDatabase tests/test_BookDatabase.cpp src/BookDatabase.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_BookDatabase Qt6::Test Qt6::Core Qt6::Gui ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_BookDatabase PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_BookDatabase COMMAND test_BookDatabase)

    add_executable(test_ArtifactStore tests/test_ArtifactStore.cpp src/store/ArtifactStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_ArtifactStore Qt6::Test Qt6::Core Qt6::Sql ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_ArtifactStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_ArtifactStore COMMAND test_ArtifactStore)

    add_executable(test_FolderStore tests/test_FolderStore.cpp src/store/FolderStore.cpp src/store/ArtifactStore.cpp src/store/RecoveryStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/BookDatabase.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_FolderStore Qt6::Test Qt6::Core Qt6::Gui ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_FolderStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_FolderStore COMMAND test_FolderStore)

    add_executable(test_RecoveryStore tests/test_RecoveryStore.cpp src/store/RecoveryStore.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/BookDatabase.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_RecoveryStore Qt6::Test Qt6::Core Qt6::Gui ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_RecoveryStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_RecoveryStore COMMAND test_RecoveryStore)

    add_executable(test_QueueManager tests/test_QueueManager.cpp src/QueueManager.cpp src/BookDatabase.cpp src/store/ActivityStore.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/store/ChatStore.cpp src/OllamaClient.cpp src/NotificationDelegate.cpp src/DocumentTemplatesManager.cpp src/AIOperationsManager.cpp src/TemplateParser.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_QueueManager Qt6::Test Qt6::Core Qt6::Widgets Qt6::Network ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_QueueManager PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_QueueManager COMMAND test_QueueManager)

    add_executable(test_PerformanceMainWindow tests/test_PerformanceMainWindow.cpp src/BookDatabase.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_PerformanceMainWindow Qt6::Test Qt6::Core Qt6::Gui Qt6::Sql ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_PerformanceMainWindow PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_PerformanceMainWindow COMMAND test_PerformanceMainWindow)

    add_executable(test_Migrations tests/test_Migrations.cpp src/BookDatabase.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_Migrations Qt6::Test Qt6::Core Qt6::Gui ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_Migrations PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_Migrations COMMAND test_Migrations)

    add_executable(test_CredentialStore tests/test_CredentialStore.cpp src/CredentialStore.cpp src/ConnectionMigration.cpp)
    target_link_libraries(test_CredentialStore Qt6::Test Qt6::Core KF6::Wallet)
    add_test(NAME test_CredentialStore COMMAND test_CredentialStore)

    add_executable(test_ConnectionMigration tests/test_ConnectionMigration.cpp src/ConnectionMigration.cpp src/CredentialStore.cpp)
    target_link_libraries(test_ConnectionMigration Qt6::Test Qt6::Core KF6::Wallet)
    add_test(NAME test_ConnectionMigration COMMAND test_ConnectionMigration)

    add_executable(test_AppCredentialManager tests/test_AppCredentialManager.cpp src/AppCredentialManager.cpp src/CredentialStore.cpp)
    target_link_libraries(test_AppCredentialManager Qt6::Test Qt6::Core KF6::Wallet)
    add_test(NAME test_AppCredentialManager COMMAND test_AppCredentialManager)

    add_executable(test_SettingsApply tests/test_SettingsApply.cpp src/SettingsDialog.cpp src/SettingsDialog.h src/AppCredentialManager.cpp src/AppCredentialManager.h src/CredentialStore.cpp src/ConnectionMigration.cpp src/AIOperationsManager.cpp src/DocumentTemplatesManager.cpp src/AIOperationsEditorWidget.cpp src/DocumentTemplatesEditorWidget.cpp src/AiActionDialog.cpp src/TemplateParser.cpp src/BookDatabase.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_SettingsApply Qt6::Test Qt6::Core Qt6::Sql Qt6::Widgets Qt6::Network KF6::Wallet KF6::ConfigWidgets KF6::CoreAddons KF6::I18n KF6::XmlGui ${SQLCIPHER_LIBRARIES})
    add_test(NAME test_SettingsApply COMMAND test_SettingsApply)

    add_executable(test_LlmProvider tests/test_LlmProvider.cpp src/OllamaClient.cpp src/DocumentTemplatesManager.cpp src/AIOperationsManager.cpp src/BookDatabase.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/PromptStore.cpp src/store/RunStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_LlmProvider Qt6::Test Qt6::Core Qt6::Gui Qt6::Network ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_LlmProvider PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_LlmProvider COMMAND test_LlmProvider)

    add_executable(test_PromptStore tests/test_PromptStore.cpp src/store/PromptStore.cpp src/store/ArtifactStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_PromptStore Qt6::Test Qt6::Core ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_PromptStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_PromptStore COMMAND test_PromptStore)

    add_executable(test_RunStore tests/test_RunStore.cpp src/store/RunStore.cpp src/store/PromptStore.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/RecoveryStore.cpp src/BookDatabase.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_RunStore Qt6::Test Qt6::Core Qt6::Gui ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_RunStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_RunStore COMMAND test_RunStore)

    add_executable(test_ChatStore tests/test_ChatStore.cpp src/store/ChatStore.cpp src/store/RunStore.cpp src/store/PromptStore.cpp src/store/ArtifactStore.cpp src/store/RecoveryStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_ChatStore Qt6::Test Qt6::Core ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_ChatStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_ChatStore COMMAND test_ChatStore)

    add_executable(test_ActivityStore tests/test_ActivityStore.cpp src/store/ActivityStore.cpp src/store/RunStore.cpp src/store/PromptStore.cpp src/store/ArtifactStore.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_ActivityStore Qt6::Test Qt6::Core ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_ActivityStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_ActivityStore COMMAND test_ActivityStore)

    add_executable(test_SearchStore tests/test_SearchStore.cpp src/store/SearchStore.cpp src/controllers/BookTreeController.cpp src/store/ChatStore.cpp src/store/RunStore.cpp src/store/PromptStore.cpp src/store/ArtifactStore.cpp src/store/FolderStore.cpp src/store/RecoveryStore.cpp src/BookDatabase.cpp src/db/Database.cpp src/db/Migrations.cpp src/db/MigrationFactory.cpp)
    target_link_libraries(test_SearchStore Qt6::Test Qt6::Core Qt6::Gui ${SQLCIPHER_LIBRARIES})
    target_include_directories(test_SearchStore PRIVATE ${SQLCIPHER_INCLUDE_DIRS})
    add_test(NAME test_SearchStore COMMAND test_SearchStore)
endif()
