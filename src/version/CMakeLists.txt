add_library(version OBJECT version.cpp)

target_compile_definitions(
  version
  PRIVATE FW_BUILD_NUMBER=${BUILD_NUMBER}
          FW_VERSION_FULL=${PROJECT_VERSION_FULL}
          FW_VERSION=${PROJECT_VERSION}
          FW_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
          FW_VERSION_MINOR=${PROJECT_VERSION_MINOR}
          FW_VERSION_PATCH=${PROJECT_VERSION_PATCH}
          FW_VERSION_SUFFIX=${PROJECT_VERSION_SUFFIX}
          FW_VERSION_SUFFIX_SHORT=${PROJECT_VERSION_SUFFIX_SHORT}
          FW_COMMIT_HASH=${FW_COMMIT_HASH}
          FW_COMMIT_DIRTY=$<BOOL:${FW_COMMIT_DIRTY}>
          PRINTER_CODE=${PRINTER_CODE}
  )

# COREONE++ project release number (the 'a' in x.y.z.a). Only defined for COREONE++ release builds
# so that PR/dev builds keep the stock version display untouched.
if(CUSTOM_VERSION)
  target_compile_definitions(version PRIVATE FW_CUSTOM_VERSION=${CUSTOM_VERSION})
endif()

target_link_libraries(version PRIVATE options printers)

target_include_directories(version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
