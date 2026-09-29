# 后端单测：一个模块一个可执行，ctest 注册（manual_A：test_<模块>.cpp）
foreach(test_name test_time_bucket test_pool test_engine)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE merging_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
