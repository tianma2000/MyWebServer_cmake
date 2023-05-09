file(GLOB_RECURSE srcs CONFIGURE_DEPENDS src/*.cpp include/*.h)
add_library(lock STATIC ${srcs})
set_target_properties(lock PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(lock PUBLIC include pthread)
