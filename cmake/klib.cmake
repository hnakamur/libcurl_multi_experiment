include(FetchContent)
fetchcontent_declare(
  klib
  GIT_REPOSITORY https://github.com/attractivechaos/klib.git
  GIT_TAG master
)
fetchcontent_makeavailable(klib)

set(KLIB_INCLUDE_DIRS ${klib_SOURCE_DIR})
