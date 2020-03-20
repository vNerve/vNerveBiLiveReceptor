include(FetchContent)
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG        v1.27.3
)
FetchContent_MakeAvailable(gRPC)
# Fix: #22090
# error C2220: the following warning is treated as an error
# warning C4577: 'noexcept' used with no exception handling mode specified; termination on exception is not guaranteed. Specify /EHsc
set_target_properties(test_support_lib PROPERTIES CXX_STANDARD 11)
set_target_properties(decrepit PROPERTIES CXX_STANDARD 11)
set_target_properties(decrepit_test PROPERTIES CXX_STANDARD 11)
set_target_properties(bssl PROPERTIES CXX_STANDARD 11)
target_compile_options(bssl PUBLIC /wd4005)
set_target_properties(bssl_shim PROPERTIES CXX_STANDARD 11)
set_target_properties(handshaker PROPERTIES CXX_STANDARD 11)
set_target_properties(ssl PROPERTIES CXX_STANDARD 11)
set_target_properties(ssl_test PROPERTIES CXX_STANDARD 11)
set_target_properties(urandom_test PROPERTIES CXX_STANDARD 11)
set_target_properties(crypto_test PROPERTIES CXX_STANDARD 11)
set_target_properties(crypto_test_data PROPERTIES CXX_STANDARD 11)
set_target_properties(boringssl_gtest PROPERTIES CXX_STANDARD 11)
set_target_properties(boringssl_gtest_main PROPERTIES CXX_STANDARD 11)
