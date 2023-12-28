find_package(CURL REQUIRED)

if(NOT CURL_FOUND)
  message(FATAL_ERROR "Curl library not found.")
endif()

add_executable(multi_ex1 main.c)
target_include_directories(multi_ex1 PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(multi_ex1 ${CURL_LIBRARIES})
