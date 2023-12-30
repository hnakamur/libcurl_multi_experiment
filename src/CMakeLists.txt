include(klib)

find_package(CURL REQUIRED)

if(NOT CURL_FOUND)
  message(FATAL_ERROR "Curl library not found.")
endif()

add_executable(mulcurloader main.c)
target_include_directories(mulcurloader PRIVATE ${CURL_INCLUDE_DIRS} ${KLIB_INCLUDE_DIRS})
target_link_libraries(mulcurloader ${CURL_LIBRARIES})
