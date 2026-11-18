# Header-only FFT library (https://github.com/mreineck/pocketfft, "cpp" branch, commit 84384cd).
add_library(pocketfft INTERFACE)
target_include_directories(pocketfft SYSTEM INTERFACE pocketfft)
target_compile_definitions(pocketfft INTERFACE POCKETFFT_NO_MULTITHREADING)
