if(HAS_MANUAL_BELT_TUNING)
  target_sources(firmware PRIVATE screen_manual_belt_tuning.cpp)
endif()

if(HAS_MOTOR_VIBRATION)
  target_sources(firmware PRIVATE screen_motor_vibration.cpp)
endif()

target_sources(firmware PRIVATE screen_selftest_fsensors.cpp)
