target_include_directories(firmware PRIVATE . include)

# G29.cpp (which calls into this module) lives in the Marlin library target, so
# the wiper header must be visible there too. The wiper symbols themselves are
# defined in the firmware target (see target_sources below) and are resolved at
# the final link, exactly like nozzle_cleaner.
if(TARGET Marlin)
    target_include_directories(Marlin PRIVATE include)
endif()

target_sources(firmware PRIVATE wiper_cleaner.cpp)
