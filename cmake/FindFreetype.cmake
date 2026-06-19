# Bundled static FreeType (lib/freetype-windows-binaries, provisioned by
# setup.bat as a junction). Used by RmlUi's font engine and our own
# find_package(Freetype). Lives under the tracked cmake/ dir (lib/ is
# gitignored) and is put on CMAKE_MODULE_PATH by the top-level CMakeLists.
set(FREETYPE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/include)
set(FREETYPE_LIBRARIES "${CMAKE_SOURCE_DIR}/lib/freetype-windows-binaries/release static/vs2015-2022/win64/freetype.lib")
add_library(Freetype::Freetype STATIC IMPORTED)
set_target_properties(Freetype::Freetype PROPERTIES
	IMPORTED_LOCATION ${FREETYPE_LIBRARIES}
)
target_include_directories(Freetype::Freetype INTERFACE
	${FREETYPE_INCLUDE_DIRS}
)
