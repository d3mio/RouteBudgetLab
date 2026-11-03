# Included right after project() via -DCMAKE_PROJECT_INCLUDE. Used ONLY to compile the unmodified
# upstream sources as C++17 (upstream's CMakeLists.txt sets CMAKE_CXX_STANDARD 14 after project()).
# A deferred call runs at the end of the top-level CMakeLists, i.e. after upstream's own settings.
if(DEFINED RBL_CXX_STANDARD)
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL set_target_properties balance PROPERTIES CXX_STANDARD ${RBL_CXX_STANDARD})
endif()
