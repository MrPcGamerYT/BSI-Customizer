# Auditable build-only correction for upstream 1.0.3's two unaligned/aliasing
# typed loads. Keep all vendored source/license bytes and their hashes intact.
# memcpy has identical match semantics, without undefined C loads. No sanitizer
# suppression, no change to compression options, and no downloaded patch tool.
if(NOT BSI_ZOPFLI_UPSTREAM_FILE OR NOT BSI_ZOPFLI_MATCH_OUTPUT)
    message(FATAL_ERROR "Specify the pinned Zopfli source and generated output")
endif()
file(SHA256 "${BSI_ZOPFLI_UPSTREAM_FILE}" original)
if(NOT original STREQUAL "fa207873b711a6bb4d271912dfa088a341f352c59accd0599a583ce7e817512c")
    message(FATAL_ERROR "Unexpected upstream lz77.c; refusing to apply the bounded encoder correction")
endif()
file(READ "${BSI_ZOPFLI_UPSTREAM_FILE}" source)
string(REPLACE "#include <stdlib.h>" "#include <stdlib.h>\n#include <string.h>\n/* BSI build-only modification: memcpy instead of two unaligned typed loads. */" source "${source}")
string(REPLACE [[while (scan < safe_end && *((size_t*)scan) == *((size_t*)match)) {]]
[[while (scan < safe_end) {
      size_t scan_word, match_word;
      memcpy(&scan_word, scan, sizeof(scan_word));
      memcpy(&match_word, match, sizeof(match_word));
      if (scan_word != match_word) break;]] source "${source}")
string(REPLACE [[while (scan < safe_end
        && *((unsigned int*)scan) == *((unsigned int*)match)) {]]
[[while (scan < safe_end) {
      unsigned int scan_word, match_word;
      memcpy(&scan_word, scan, sizeof(scan_word));
      memcpy(&match_word, match, sizeof(match_word));
      if (scan_word != match_word) break;]] source "${source}")
string(SHA256 corrected "${source}")
if(NOT corrected STREQUAL "557de25382d40718ef590ed8d71056d247f9b2843081d3f37845c64a6291fa95")
    message(FATAL_ERROR "Unexpected corrected lz77.c bytes; refusing to compile")
endif()
# file(WRITE) can translate LF to native CRLF on Windows. configure_file's
# explicit newline mode produces the exact pinned bytes on every platform.
file(WRITE "${BSI_ZOPFLI_MATCH_OUTPUT}.in" "${source}")
configure_file("${BSI_ZOPFLI_MATCH_OUTPUT}.in" "${BSI_ZOPFLI_MATCH_OUTPUT}" @ONLY NEWLINE_STYLE UNIX)
file(SHA256 "${BSI_ZOPFLI_MATCH_OUTPUT}" written)
if(NOT written STREQUAL corrected)
    message(FATAL_ERROR "Generated match-source file hash differs from the verified bytes")
endif()
