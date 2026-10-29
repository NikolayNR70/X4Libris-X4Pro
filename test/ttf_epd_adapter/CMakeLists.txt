set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(FONT_ROOT "${REPO_ROOT}/freeink-sdk/libs/font/FreeInkFont")
file(GLOB FT_WRAPPER_SOURCES CONFIGURE_DEPENDS "${FONT_ROOT}/src/freetype/*.c")

add_executable(TtfEpdAdapterTest
  ${REPO_ROOT}/test/host/TtfEpdAdapterTest.cpp
  ${REPO_ROOT}/lib/EpdFont/TtfEpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/GlyphFallback.cpp
  ${REPO_ROOT}/lib/EpdFont/RawTtfFontManager.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${FONT_ROOT}/src/FtFont.cpp
  ${FONT_ROOT}/src/FontAlloc.c
  ${FONT_ROOT}/src/Gpos.cpp
  ${FONT_ROOT}/src/Gsub.cpp
  ${FT_WRAPPER_SOURCES}
)

target_include_directories(TtfEpdAdapterTest PRIVATE
  ${REPO_ROOT}/test/host/ttf_epd_adapter_stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${FONT_ROOT}/include
  ${FONT_ROOT}/third_party/freetype/include
  ${FONT_ROOT}/third_party/stb
)
target_compile_definitions(TtfEpdAdapterTest PRIVATE
  BOARD_HAS_PSRAM=1
  FREEINK_FONT_ENABLE_CFF=1
  FREEINK_FONT_ENABLE_PSNAMES=1
)
target_link_libraries(TtfEpdAdapterTest PRIVATE crosspoint_test_common)

add_test(NAME TtfEpdAdapterDirectFiles
  COMMAND TtfEpdAdapterTest
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/Ubuntu/Ubuntu-Regular.ttf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/Ubuntu/Ubuntu-Bold.ttf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-Regular.otf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-Bold.otf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-Italic.otf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/OpenDyslexic/OpenDyslexic-BoldItalic.otf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/Inter/Inter-Regular.ttf
    ${REPO_ROOT}/lib/EpdFont/builtinFonts/source/Inter/Inter-Bold.ttf
)
