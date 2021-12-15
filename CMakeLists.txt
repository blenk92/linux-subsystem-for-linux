cmake_minimum_required(VERSION 3.14)
project(lsl)

set(CMAKE_CXX_STANDARD 17)

FIND_PACKAGE( Boost 1.40 COMPONENTS system program_options REQUIRED )
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
set(EXECUTORPATH "${INSTALLDIR}/lslExecutor")

if ("${CONFIGPATH}" STREQUAL "")
    set(CONFIGPATH "/etc/subsys.conf")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FORTIFY_SOURCE=2 -O3 -Wl,-z,relro,-z,now")

configure_file(common.h.in common.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(lsl lsl.cpp common.cpp)
target_link_libraries(lsl LINK_PUBLIC ${Boost_LIBRARIES} stdc++fs pthread cap seccomp)
install(TARGETS lsl
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION ${INSTALLDIR}
        )

add_executable(lslExecutor executor.cpp common.cpp)
target_link_libraries(lslExecutor stdc++fs cap)
install(TARGETS lslExecutor
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE SETUID GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        DESTINATION ${INSTALLDIR}
        )
