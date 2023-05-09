file(GLOB_RECURSE srcs CONFGIURE_DEPENDS src/*.cpp include/*.h)
add_library(cgimysql STATIC ${srcs})
target_include_directories(cgimysql PUBLIC include)



target_link_libraries(cgimysql PUBLIC log lock mysqlclient)
