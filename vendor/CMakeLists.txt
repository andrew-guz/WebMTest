add_subdirectory(Crow)
target_compile_options(Crow INTERFACE -Wno-deprecated-declarations)
target_compile_definitions(Crow INTERFACE CROW_DISABLE_STATIC_DIR)
target_precompile_headers(Crow INTERFACE <crow.h>)
