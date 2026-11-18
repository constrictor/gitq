cmake_minimum_required(VERSION 3.21)
project(gitq VERSION 0.1.0 LANGUAGES CXX)

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

add_library(gitq_ui STATIC
  src/ui/MainWindow.h src/ui/MainWindow.cpp
  src/ui/CommitListView.h src/ui/CommitListView.cpp
  src/ui/GraphDelegate.h src/ui/GraphDelegate.cpp
  src/ui/DiffView.h src/ui/DiffView.cpp
  src/ui/ViewDialog.h src/ui/ViewDialog.cpp
  src/ui/PrefsDialog.h src/ui/PrefsDialog.cpp
  src/ui/Dialogs.h src/ui/Dialogs.cpp
  src/ui/BlameWindow.h src/ui/BlameWindow.cpp
)
target_link_libraries(gitq_ui PUBLIC gitq_core)
target_compile_options(gitq_ui PRIVATE -Wall -Wextra)
target_compile_definitions(gitq_ui PUBLIC PROJECT_VERSION="${PROJECT_VERSION}")

add_executable(gitq src/main.cpp)
target_link_libraries(gitq PRIVATE gitq_ui)
target_compile_options(gitq PRIVATE -Wall -Wextra)
install(TARGETS gitq)

enable_testing()
add_subdirectory(tests)
