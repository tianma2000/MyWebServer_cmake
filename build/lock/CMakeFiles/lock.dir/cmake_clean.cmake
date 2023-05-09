file(REMOVE_RECURSE
  "liblock.a"
  "liblock.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/lock.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
