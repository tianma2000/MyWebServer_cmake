file(GLOB_RECURSE srcs CONFIGURE_DEPENDS include/*.h src/*.cpp)
add_library(threadpool STATIC ${srcs})
set_target_properties(threadpool PROPERTIES LINKER_LANGUAGE CXX) 
target_include_directories(threadpool PUBLIC include)

target_link_libraries(threadpool PUBLIC lock cgimysql)
