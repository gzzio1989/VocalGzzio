# 独立した部品検査。古いシェル専用の手動リンクは使わない。
set(gz_unit_tests
    dsp_ornament dsp_hum dsp_consonant dsp_resonance dsp_popclick
    dsp_ride dsp_mixalign dsp_proximity dsp_longrun dsp_vc_uv dsp_heya
    dsp_voiceshift_stress dsp_pitchquality dsp_tdshift test_4hour_crackle
    dsp_driver_loopback)

set(gz_integration_tests
    dsp_noalloc dsp_checkup dsp_dnlearn dsp_dnstartup dsp_dnpreinput dsp_srcmode dsp_rt_alloc dsp_cpu
    dsp_dnonset dsp_belt dsp_autoset dsp_modules dsp_crush dsp_order
    dsp_host dsp_dnfade dsp_swish dsp_usemode dsp_usememo dsp_pick
    dsp_rates ui_fit ui_usemode ui_focus ui_load dsp_dnslow dsp_dndrift
    dsp_dngate dsp_dnrestore dsp_dnlearnrep dsp_heyawire dsp_stereo dsp_presets ui_overview ui_tuner ui_navigation
    dsp_longrun_full dsp_v550_features dsp_crackle_stress dsp_mastering_pro_suite ui_horology_and_science_audit dsp_live_se_and_nextgen_audit dsp_human_resonance_and_safety_audit
    dsp_supreme_safety_and_lightweight_audit dsp_supreme_dedup_and_human_emotion_audit ui_supreme_horology_and_accessibility_audit
    dsp_sovereign_safety_lightweight_and_dedup_audit dsp_sovereign_soul_studio_and_streamer_audit ui_sovereign_horology_wcag_and_layout_audit
    dsp_celestial_safety_lightweight_and_dedup_audit dsp_celestial_soul_studio_and_streamer_audit ui_celestial_horology_wcag_and_layout_audit)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dsp_dnquality.cpp")
    list(APPEND gz_integration_tests dsp_dnquality)
endif()
if (VOCALGZZIO_TRIAL)
    # 製品版固有の保存/復元条件を体験版に当てはめない。
    set(gz_integration_tests dsp_noalloc dsp_trial)
endif()

function(gz_configure_check name)
    target_compile_features(${name} PRIVATE cxx_std_17)
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/Source")
    target_compile_definitions(${name} PRIVATE _USE_MATH_DEFINES)
    if (MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /EHsc)
        target_link_options(${name} PRIVATE /STACK:8388608)
    endif()
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endfunction()

function(gz_link_plugin name)
    # JUCE の INTERFACE_SOURCES を再コンパイルせず、実際の本体へリンク。
    target_link_libraries(${name} PRIVATE VocalGzzio)
    target_include_directories(${name} PRIVATE
        "$<TARGET_PROPERTY:VocalGzzio,INCLUDE_DIRECTORIES>")
    target_compile_definitions(${name} PRIVATE
        "$<TARGET_PROPERTY:VocalGzzio,COMPILE_DEFINITIONS>")
endfunction()

foreach(name IN LISTS gz_unit_tests gz_integration_tests)
    add_executable(${name} "${name}.cpp")
    gz_configure_check(${name})
    if (name IN_LIST gz_integration_tests)
        gz_link_plugin(${name})
    endif()
    add_test(NAME ${name} COMMAND ${name})
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-data/${name}")
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 300
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/test-data/${name}"
        ENVIRONMENT "VOCALGZZIO_TEST_DATA_DIR=${CMAKE_BINARY_DIR}/test-data/${name}")
endforeach()
add_custom_target(VocalGzzioTests DEPENDS ${gz_unit_tests} ${gz_integration_tests})

# 見た目の確認用。数値検査の合否には画像の生成を混ぜない。
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui_snapshot.cpp")
    add_executable(ui_snapshot ui_snapshot.cpp)
    gz_configure_check(ui_snapshot)
    gz_link_plugin(ui_snapshot)
endif()

# 検査環境の不足を「成功」として扱わない。
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME check_revtype
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_revtype.py")
set_tests_properties(check_revtype PROPERTIES ENVIRONMENT "PYTHONUTF8=1")
