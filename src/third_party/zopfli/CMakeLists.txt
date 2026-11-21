# Pinned, byte-unmodified Apache-2.0 encoder; see UPSTREAM.json and LICENSE.
# Force-remap ONLY upstream allocation/exit calls into the C guard. The guard
# itself must use the real CRT, never these macros or a C++ longjmp frame.
if(NOT TARGET bsi_zopfli)
    set(BSI_ZOPFLI_UPSTREAM_FILE "${CMAKE_CURRENT_LIST_DIR}/lz77.c")
    set(BSI_ZOPFLI_MATCH_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bsi_lz77.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BSI_ZOPFLI_UPSTREAM_FILE}")
    include("${CMAKE_CURRENT_LIST_DIR}/../../core/ZopfliMatch.cmake")
    add_library(bsi_zopfli_encoder OBJECT blocksplitter.c cache.c deflate.c hash.c
        katajainen.c "${BSI_ZOPFLI_MATCH_OUTPUT}" squeeze.c tree.c util.c zlib_container.c)
    target_include_directories(bsi_zopfli_encoder PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    set(allocator "${CMAKE_CURRENT_LIST_DIR}/../../core/ZopfliAllocator.h")
    if(MSVC)
        target_compile_options(bsi_zopfli_encoder PRIVATE "/FI${allocator}")
    else()
        target_compile_options(bsi_zopfli_encoder PRIVATE -include "${allocator}")
    endif()
    add_library(bsi_zopfli STATIC $<TARGET_OBJECTS:bsi_zopfli_encoder> ../../core/BoundedZopfli.c)
    foreach(target bsi_zopfli bsi_zopfli_encoder)
        set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    endforeach()
    # Enabled only by isolated shader-test projects, never by the application.
    if(BSI_ZOPFLI_TESTING)
        target_compile_definitions(bsi_zopfli PRIVATE BSI_ZOPFLI_TESTING)
    endif()
    if(NOT WIN32)
        target_link_libraries(bsi_zopfli PUBLIC m)
    endif()
endif()
