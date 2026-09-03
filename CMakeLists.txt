cmake_minimum_required(VERSION 3.16)
project(CrashVault C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CRASHVAULT_WARNING_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wformat=2
    -Wnull-dereference
)

find_program(CRASHVAULT_ADDR2LINE NAMES addr2line REQUIRED)

# --- SQLite ---
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    set(FETCHCONTENT_BASE_DIR "/tmp/crashvault_fetch")
    include(FetchContent)
    FetchContent_Declare(
        sqlite_amalgamation
        URL "https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip"
    )
    FetchContent_GetProperties(sqlite_amalgamation)
    if(NOT sqlite_amalgamation_POPULATED)
        FetchContent_Populate(sqlite_amalgamation)
        add_library(sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
        target_include_directories(sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
        target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1)
    endif()
    set(CRASHVAULT_SQLITE_LIB sqlite3)
else()
    add_library(crashvault_sqlite INTERFACE)
    target_link_libraries(crashvault_sqlite INTERFACE SQLite::SQLite3)
    set(CRASHVAULT_SQLITE_LIB crashvault_sqlite)
endif()

set(CRASHVAULT_SDK_SOURCES native/crashvault.c)

add_library(crashvault STATIC ${CRASHVAULT_SDK_SOURCES})
target_include_directories(crashvault PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_compile_options(crashvault PRIVATE ${CRASHVAULT_WARNING_FLAGS})
target_link_libraries(crashvault PUBLIC dl)

set(CRASHVAULT_PROCESSOR_SOURCES
    native/processor.c
    native/symbolize.c
)

add_library(crashvault_processor STATIC ${CRASHVAULT_PROCESSOR_SOURCES})
target_include_directories(crashvault_processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/native)
target_link_libraries(crashvault_processor PUBLIC ${CRASHVAULT_SQLITE_LIB})
target_compile_options(crashvault_processor PRIVATE ${CRASHVAULT_WARNING_FLAGS})
target_compile_definitions(crashvault_processor PRIVATE CRASHVAULT_ADDR2LINE_PATH="${CRASHVAULT_ADDR2LINE}")

add_executable(crashvault-process native/processor_main.c)
target_link_libraries(crashvault-process PRIVATE crashvault_processor)
target_compile_options(crashvault-process PRIVATE ${CRASHVAULT_WARNING_FLAGS})

# --- Installation (SDK + processor CLI) ---
include(GNUInstallDirs)

install(TARGETS crashvault crashvault-process
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    native/crashvault.h
    native/crashvault_report.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/crashvault
)

install(FILES README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    OPTIONAL
)
