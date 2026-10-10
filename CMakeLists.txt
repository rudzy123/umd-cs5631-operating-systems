cmake_minimum_required(VERSION 3.16)

project(umd-cs5631-operating-systems
    VERSION 1.0.0
    DESCRIPTION "CS 5631 operating systems laboratory"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(OSLAB_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(OSLAB_TSAN "Build with ThreadSanitizer (incompatible with OSLAB_SANITIZERS)" OFF)

if(OSLAB_SANITIZERS AND OSLAB_TSAN)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be combined in one build")
endif()

find_package(Threads REQUIRED)

add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
)

if(OSLAB_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

if(OSLAB_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=thread)
endif()

add_library(oslab
    src/bounded_buffer.c
    src/deadlock.c
    src/fsutil.c
    src/scheduler.c
    src/vm.c
)
target_include_directories(oslab PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(oslab PUBLIC Threads::Threads)

add_executable(process_demo src/process_demo.c)

function(oslab_program target)
    add_executable(${target} "src/${target}.c")
    target_link_libraries(${target} PRIVATE oslab)
endfunction()

oslab_program(thread_demo)
oslab_program(scheduler_demo)
oslab_program(deadlock_demo)
oslab_program(vm_demo)
oslab_program(fs_demo)

enable_testing()

function(oslab_add_test target)
    add_executable(${target} "tests/${target}.c")
    target_link_libraries(${target} PRIVATE oslab)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 20)
endfunction()

oslab_add_test(test_scheduler)
oslab_add_test(test_vm)
oslab_add_test(test_bounded_buffer)
oslab_add_test(test_fsutil)
oslab_add_test(test_deadlock)

add_test(NAME process_demo COMMAND process_demo)
add_test(NAME thread_demo COMMAND thread_demo)
add_test(NAME scheduler_demo COMMAND scheduler_demo)
add_test(NAME deadlock_demo COMMAND deadlock_demo)
add_test(NAME vm_demo COMMAND vm_demo)
add_test(NAME fs_demo_stat COMMAND fs_demo stat /tmp)
set_tests_properties(
    process_demo
    thread_demo
    scheduler_demo
    deadlock_demo
    vm_demo
    fs_demo_stat
    PROPERTIES TIMEOUT 20
)

# Opposite-order locking is bounded by pthread_mutex_timedlock. ThreadSanitizer
# reports that inversion even when the wait times out, so this check runs only
# in builds that are not instrumented with TSan. thread_demo --unsafe is a
# deliberate data race and is never a ctest target.
if(NOT OSLAB_TSAN)
    add_test(NAME deadlock_inversion COMMAND deadlock_demo --unsafe-inversion)
    set_tests_properties(deadlock_inversion PROPERTIES TIMEOUT 10)
endif()
