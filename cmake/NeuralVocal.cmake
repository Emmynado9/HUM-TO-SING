# Core analysis remains testable without Qt or an installed neural runtime.
set(HUM_ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/build/neural-vocal/onnxruntime-osx-x86_64-1.20.1"
    CACHE PATH "Native ONNX Runtime headers and libraries for the separate vocal engine")
add_library(hum_vocal STATIC src/vocal/transcription.cpp src/vocal/neural_pitch.cpp)
target_include_directories(hum_vocal PUBLIC "${CMAKE_SOURCE_DIR}/include")
if(EXISTS "${HUM_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    if(APPLE)
        set(HUM_VOCAL_RUNTIME "${HUM_ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.20.1.dylib")
    elseif(WIN32)
        set(HUM_VOCAL_RUNTIME "${HUM_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
    else()
        set(HUM_VOCAL_RUNTIME "${HUM_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
    endif()
    if(EXISTS "${HUM_VOCAL_RUNTIME}")
        target_include_directories(hum_vocal SYSTEM PRIVATE "${HUM_ONNXRUNTIME_ROOT}/include")
        target_compile_definitions(hum_vocal PRIVATE HUM_VOCAL_WITH_ONNX=1
            HUM_VOCAL_RUNTIME_PATH="${HUM_VOCAL_RUNTIME}")
        target_link_libraries(hum_vocal PRIVATE ${CMAKE_DL_LIBS})
        set(HUM_VOCAL_BACKEND_AVAILABLE ON)
    endif()
endif()

function(hum_bundle_vocal target)
    qt_add_resources(${target} vocal_model PREFIX "/vocal" BASE "${CMAKE_SOURCE_DIR}/resources/vocal"
        FILES "${CMAKE_SOURCE_DIR}/resources/vocal/swift-f0.onnx")
    if(APPLE AND HUM_VOCAL_BACKEND_AVAILABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/../Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HUM_VOCAL_RUNTIME}"
                "$<TARGET_FILE_DIR:${target}>/../Frameworks/libonnxruntime.1.20.1.dylib")
    endif()
endfunction()
if(BUILD_TESTING)
    add_executable(hum_vocal_transcription_tests tests/vocal_transcription_tests.cpp)
    target_link_libraries(hum_vocal_transcription_tests PRIVATE hum_vocal)
    add_test(NAME hum_vocal_transcription_tests COMMAND hum_vocal_transcription_tests)
    set_tests_properties(hum_vocal_transcription_tests PROPERTIES LABELS "core;vocal")
    if(HUM_VOCAL_BACKEND_AVAILABLE)
        add_executable(hum_vocal_inference_tests tests/vocal_inference_tests.cpp)
        target_link_libraries(hum_vocal_inference_tests PRIVATE hum_vocal)
        target_compile_definitions(hum_vocal_inference_tests PRIVATE
            HUM_VOCAL_MODEL_FILE="${CMAKE_SOURCE_DIR}/resources/vocal/swift-f0.onnx")
        add_test(NAME hum_vocal_inference_tests COMMAND hum_vocal_inference_tests)
        add_test(NAME hum_vocal_missing_runtime_tests COMMAND hum_vocal_inference_tests --missing-runtime)
        set_tests_properties(hum_vocal_missing_runtime_tests PROPERTIES LABELS "vocal;neural" TIMEOUT 15)
        set_tests_properties(hum_vocal_inference_tests PROPERTIES LABELS "vocal;neural" TIMEOUT 90)
    endif()
endif()
