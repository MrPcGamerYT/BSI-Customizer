# Pinned, unmodified Apache-2.0 Zopfli encoder; see UPSTREAM.json and LICENSE.
# Only the zlib encoder is built, never the command-line utility or gzip/PNG.
if(NOT TARGET bsi_zopfli)
    add_library(bsi_zopfli STATIC blocksplitter.c cache.c deflate.c hash.c
        katajainen.c lz77.c squeeze.c tree.c util.c zlib_container.c)
    set_target_properties(bsi_zopfli PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(NOT WIN32)
        target_link_libraries(bsi_zopfli PUBLIC m)
    endif()
endif()
