file(GLOB_RECURSE srcs CONFIG_DEPENDS src/*.cpp include/*.h)
add_library(http STATIC ${srcs})
target_include_directories(http PUBLIC include)

target_link_libraries(http PUBLIC log lock cgimysql)
