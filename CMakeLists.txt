cmake_minimum_required(VERSION 3.14)
project(subsystem)

set(CMAKE_CXX_STANDARD 17)

FIND_PACKAGE( Boost 1.40 COMPONENTS system REQUIRED )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

if ("${MNTDIR}" STREQUAL "")
    set(MNTDIR "/tmp/subsys")
endif()

if ("${LINKSDIR}" STREQUAL "")
    set(LINKSDIR "/subsysbin")
endif()

if ("${INSTALLDIR}" STREQUAL "")
    set(INSTALLDIR "/bin")
endif()
set(EXECUTORPATH "${INSTALLDIR}/subsystemExecutor")

if ("${CONFIGPATH}" STREQUAL "")
    set(CONFIGPATH "/etc/subsys.conf")
endif()

configure_file(common.h.in common.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(subsystem subsystem.cpp)
target_link_libraries(subsystem LINK_PUBLIC ${Boost_LIBRARIES} stdc++fs pthread)
install(TARGETS subsystem
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION ${INSTALLDIR}
        )

add_executable(subsystemExecutor executor.cpp)
target_link_libraries(subsystemExecutor stdc++fs)
install(TARGETS subsystemExecutor
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE SETUID GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION ${INSTALLDIR}
        )
