# Rubber Band Library from its single-file build (built-in FFT and resampler,
# vDSP on Apple platforms).
#
# It is always built optimised: an unoptimised R3 stretch runs several times
# slower than real time, which makes debug builds and their tests crawl. MSVC
# will not optimise with run-time checks on, so they are taken out of this
# directory's debug flags (only this directory's), with the flags that turn
# optimisation off.
string(REGEX REPLACE "/RTC[1csu]+|/Od|/Ob0" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

add_library(rubberband STATIC ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)
target_include_directories(rubberband SYSTEM PUBLIC ${rubberband_SOURCE_DIR})
target_compile_features(rubberband PRIVATE cxx_std_17)
set_target_properties(rubberband PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(rubberband PRIVATE /O2 /W0 /wd4996)
    target_compile_definitions(rubberband PRIVATE _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(rubberband PRIVATE -O2 -w)
endif()

if(APPLE)
    target_link_libraries(rubberband PUBLIC "-framework Accelerate")
endif()
