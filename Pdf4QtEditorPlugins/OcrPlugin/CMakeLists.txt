# PDF Fire: Recognize Text (OCR) (MIT). Uses Tesseract (Apache 2.0) - the plugin is not
# built, when Tesseract (headers + pkg-config file, package libtesseract-dev) is not found.

# Windows (vcpkg): the Tesseract CMake package; Linux: pkg-config. (Fedora's tesseract-devel
# has a CMake package too, but its target needs libcurl-devel - pkg-config needs nothing more.)
if(WIN32)
    find_package(Tesseract CONFIG QUIET)
endif()
if(WIN32 AND Tesseract_FOUND)
    set(TESSERACT_FOUND ON)
    set(PDFFIRE_TESSERACT_TARGET Tesseract::libtesseract)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract)
    endif()
    set(PDFFIRE_TESSERACT_TARGET PkgConfig::TESSERACT)
endif()

if(NOT TESSERACT_FOUND)
    message(STATUS "OcrPlugin: Tesseract was not found - the OCR plugin is not built")
    return()
endif()

add_library(OcrPlugin SHARED
    ocrplugin.cpp
    ocrdialog.cpp
    ocrengine.cpp
    ocrjob.cpp
    ocrtextlayer.cpp
    icons.qrc
)

target_link_libraries(OcrPlugin PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Qt6::Core Qt6::Gui Qt6::Widgets ${PDFFIRE_TESSERACT_TARGET})

set_target_properties(OcrPlugin PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_PLUGINS_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_PLUGINS_DIR})

install(TARGETS OcrPlugin RUNTIME DESTINATION ${PDF4QT_PLUGINS_DIR} LIBRARY DESTINATION ${PDF4QT_PLUGINS_DIR})
