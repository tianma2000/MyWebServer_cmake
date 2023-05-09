file(GLOB_RECURSE srcs CONFIGURE_DEPENDS src/*.cpp inlude/*.h)
add_library(log STATIC ${srcs})
target_include_directories(log PUBLIC include)

target_link_libraries(log PUBLIC lock pthread) 
