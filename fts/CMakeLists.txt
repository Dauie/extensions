# jieba segments Chinese text, reading its dictionaries straight from files rather than through
# the engine's file system. Embedders that confine file access can leave it out.
option(FTS_JIEBA "Build the jieba tokenizer for Chinese text." ON)

include_directories(
        ${PROJECT_SOURCE_DIR}/src/include
        ${CMAKE_BINARY_DIR}/src/include
        src/include
        third_party/snowball/libstemmer)
if (FTS_JIEBA)
    add_compile_definitions(LBUG_FTS_JIEBA)
    include_directories(
            ${PROJECT_SOURCE_DIR}/third_party/cppjieba/include
            ${PROJECT_SOURCE_DIR}/third_party/cppjieba/deps/limonp/include)
endif ()

add_subdirectory(src/catalog)
add_subdirectory(src/function)
add_subdirectory(src/main)
add_subdirectory(test)
add_subdirectory(src/index)
add_subdirectory(src/utils)

add_subdirectory(third_party/snowball)

build_extension_lib(${BUILD_STATIC_EXTENSION} "fts")

target_link_libraries(lbug_${EXTENSION_LIB_NAME}_extension
        PRIVATE
        snowball
        re2)

if (FTS_JIEBA)
    target_link_libraries(lbug_${EXTENSION_LIB_NAME}_extension PRIVATE cppjieba)
    # Copy Jieba dictionaries next to the built extension so runtime can find a default path
    add_custom_command(TARGET lbug_${EXTENSION_LIB_NAME}_extension POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/third_party/cppjieba/dict
            ${PROJECT_SOURCE_DIR}/extension/fts/build/dict)
endif ()
