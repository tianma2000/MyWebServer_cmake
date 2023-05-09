file(REMOVE_RECURSE
  "libthreadpool.a"
  "libthreadpool.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/threadpool.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
