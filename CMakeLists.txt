cmake_minimum_required(VERSION 3.28)
project(gitq VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2>=1.7)

add_library(gitq_core STATIC
  src/git/GitPtr.h
  src/git/Oid.h
  src/git/CommitInfo.h
  src/git/Repository.h src/git/Repository.cpp
  src/git/RevArgs.h src/git/RevArgs.cpp
  src/git/LogLoader.h src/git/LogLoader.cpp
  src/git/DiffEngine.h src/git/DiffEngine.cpp
  src/git/Ops.h src/git/Ops.cpp
  src/git/GitCli.h src/git/GitCli.cpp
  src/git/Staging.h src/git/Staging.cpp
  src/git/Search.h src/git/Search.cpp
  src/git/Blame.h src/git/Blame.cpp
  src/git/NearbyRefs.h src/git/NearbyRefs.cpp
  src/graph/GraphLayout.h src/graph/GraphLayout.cpp
  src/model/CommitModel.h src/model/CommitModel.cpp
  src/model/FileListModel.h src/model/FileListModel.cpp
  src/Settings.h src/Settings.cpp
)
target_include_directories(gitq_core PUBLIC src)
target_link_libraries(gitq_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent PkgConfig::LIBGIT2)
target_compile_options(gitq_core PRIVATE -Wall -Wextra)

# Qt Advanced Docking System: Visual Studio style dockable panes (static, built from source).
include(FetchContent)
set(QT_VERSION_MAJOR 6)
set(BUILD_STATIC ON)
set(BUILD_EXAMPLES OFF)
FetchContent_Declare(qtads
  URL https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/archive/refs/tags/v5.1.1.tar.gz
  URL_HASH SHA256=9ce947acbb1984946f9145c976e42e6984f99b98fb414dd7354a290af6d4ff04
  # Floating panes on Wayland: frameless, docked by dragging their title bar (see the patch).
  PATCH_COMMAND patch -p1 -i ${CMAKE_CURRENT_SOURCE_DIR}/patches/qtads-wayland-floating.patch
  EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(qtads)

add_library(gitq_ui STATIC
  src/ui/MainWindow.h src/ui/MainWindow.cpp
  src/ui/RepoTab.h src/ui/RepoTab.cpp
  src/ui/WelcomePage.h src/ui/WelcomePage.cpp
  src/ui/CommitListView.h src/ui/CommitListView.cpp
  src/ui/GraphDelegate.h src/ui/GraphDelegate.cpp
  src/ui/Glyphs.h src/ui/Glyphs.cpp
  src/ui/Choice.h
  src/ui/RefPills.h src/ui/RefPills.cpp
  src/ui/Syntax.h src/ui/Syntax.cpp
  src/ui/FileListDelegate.h src/ui/FileListDelegate.cpp
  src/ui/DiffView.h src/ui/DiffView.cpp
  src/ui/ViewDialog.h src/ui/ViewDialog.cpp
  src/ui/PrefsDialog.h src/ui/PrefsDialog.cpp
  src/ui/Dialogs.h src/ui/Dialogs.cpp
  src/ui/BlameWindow.h src/ui/BlameWindow.cpp
  src/ui/CommitPane.h src/ui/CommitPane.cpp
  src/ui/Theme.h src/ui/Theme.cpp
)
target_link_libraries(gitq_ui PUBLIC gitq_core ads::qtadvanceddocking-qt6)
target_compile_options(gitq_ui PRIVATE -Wall -Wextra)
target_compile_definitions(gitq_ui PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")

add_executable(gitq src/main.cpp)
target_link_libraries(gitq PRIVATE gitq_ui)
target_compile_options(gitq PRIVATE -Wall -Wextra)
include(GNUInstallDirs)
# COMPONENT gitq keeps the ADS install rules (headers, static lib) out of the packages.
install(TARGETS gitq COMPONENT gitq)
install(FILES packaging/gitq.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT gitq)
install(FILES packaging/gitq.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT gitq)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT gitq)

# Packaging: `cpack -G DEB` in the build directory
set(CPACK_PACKAGE_NAME gitq)
set(CPACK_PACKAGE_VENDOR "gitq")
set(CPACK_PACKAGE_CONTACT "Valentyn Pavliuchenko" CACHE STRING "Package maintainer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Git history browser (gitk reimplemented in Qt)")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/constrictor/gitq")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_DEBIAN_PACKAGE_SECTION vcs)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# xcb/wayland platform plugins (not seen by shlibdeps); git for staging and committing.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, git")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
set(CPACK_COMPONENTS_ALL gitq)
include(CPack)

enable_testing()
add_subdirectory(tests)
