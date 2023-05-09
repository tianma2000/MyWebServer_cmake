file(GLOB_RECURSE srcs CONFIG_DEPENDS include/*.h src/*.cpp)
add_library(timer STATIC ${srcs})
target_include_directories(timer PUBLIC include)
