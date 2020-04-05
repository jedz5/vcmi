project(GeniusAI)
cmake_minimum_required(VERSION 2.6)

include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_HOME_DIRECTORY} ${CMAKE_HOME_DIRECTORY}/include ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_HOME_DIRECTORY}/lib)

set(battleAI_SRCS
		StdInc.cpp
		GeniusAI.cpp
		main.cpp
)

add_library(GeniusAI SHARED ${battleAI_SRCS})
target_link_libraries(GeniusAI vcmi)

set_target_properties(GeniusAI PROPERTIES ${PCH_PROPERTIES})
cotire(GeniusAI)

if (NOT APPLE) # Already inside vcmiclient bundle
    install(TARGETS GeniusAI RUNTIME DESTINATION ${AI_LIB_DIR} LIBRARY DESTINATION ${AI_LIB_DIR})
endif()

