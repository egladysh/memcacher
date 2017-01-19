cmake_minimum_required (VERSION 2.6)

project(memcacher)

file(GLOB src *.cpp *.hpp *.h)

if( APPLE )
	message( STATUS "Making OSX")

	#debug options
	set( CMAKE_CXX_FLAGS "-std=c++1y -stdlib=libc++ -g" )

	#release options
	#set( CMAKE_CXX_FLAGS "-std=c++1y -stdlib=libc++ -O3 -DNDEBUG")

else()

	#debug options
	#set( CMAKE_CXX_FLAGS "-std=c++1y -g -DUSE_EPOLL" )

	#release options
	set( CMAKE_CXX_FLAGS "-std=c++1y -O3 -DNDEBUG -DUSE_EPOLL" )

	list(REMOVE_ITEM src ${CMAKE_CURRENT_SOURCE_DIR}/kqepoll.cpp)
endif()



add_executable(memcacher ${src} )

	target_link_libraries( memcacher
		pthread
	)

