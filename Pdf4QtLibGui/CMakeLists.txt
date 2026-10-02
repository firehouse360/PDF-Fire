# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

add_library(Pdf4QtLibGui SHARED
    pdfaboutdialog.cpp
    pdfaboutdialog.h
    pdfadvancedfindwidget.cpp
    pdfadvancedfindwidget.h
    pdfdocumentpropertiesdialog.cpp
    pdfdocumentpropertiesdialog.h
    pdfencryptionsettingsdialog.cpp
    pdfencryptionsettingsdialog.h
    pdfencryptionstrengthhintwidget.cpp
    pdfencryptionstrengthhintwidget.h
    pdfoptimizedocumentdialog.cpp
    pdfoptimizedocumentdialog.h
    pdfoptimizeimagesdialog.cpp
    pdfoptimizeimagesdialog.h
    pdfprogramcontroller.cpp
    pdfprogramcontroller.h
    pdfprogramcontroller_pages.cpp
    pdfrecentfilemanager.cpp
    pdfrecentfilemanager.h
    pdfrendertoimagesdialog.cpp
    pdfrendertoimagesdialog.h
    pdfsendmail.cpp
    pdfsendmail.h
    pdfsidebarwidget.cpp
    pdfsidebarwidget.h
    pdftexttospeech.cpp
    pdftexttospeech.h
    pdfundoredomanager.cpp
    pdfundoredomanager.h
    pdfeditormainwindow.cpp
    pdfeditormainwindow.h
    pdfeditormainwindow_ribbon.cpp
    pdffireribbon.cpp
    pdffireribbon.h
    pdffiretheme.cpp
    pdffiretheme.h
    pdffireemaildialog.cpp
    pdffireemaildialog.h
    pdffireprintdialog.cpp
    pdffireprintdialog.h
    pdffirewelcome.cpp
    pdffirewelcome.h
    pdffire.qrc
    pdfviewermainwindow.cpp
    pdfviewermainwindow.h
    pdfviewersettings.cpp
    pdfviewersettings.h
    pdfviewersettingsdialog.cpp
    pdfviewersettingsdialog.h
    pdfwintaskbarprogress.cpp
    pdfwintaskbarprogress.h
    pdfaboutdialog.ui
    pdfadvancedfindwidget.ui
    pdfdocumentpropertiesdialog.ui
    pdfencryptionsettingsdialog.ui
    pdfoptimizedocumentdialog.ui
    pdfoptimizeimagesdialog.ui
    pdfrendertoimagesdialog.ui
    pdfsidebarwidget.ui
    pdfeditormainwindow.ui
    pdfviewermainwindow.ui
    pdfviewersettingsdialog.ui
    pdfsanitizedocumentdialog.ui
    pdfsanitizedocumentdialog.cpp
    pdfsanitizedocumentdialog.h
    pdfcreatebitonaldocumentdialog.ui
    pdfcreatebitonaldocumentdialog.cpp
    pdfcreatebitonaldocumentdialog.h
    pdf4qtlibgui.qrc
    pdfbookmarkmanager.h
    pdfbookmarkmanager.cpp
    pdfbookmarkui.h
    pdfbookmarkui.cpp
    pdfactioncombobox.h
    pdfactioncombobox.cpp
    pdffullscreenwidget.h
    pdffullscreenwidget.cpp
    pdffireg2p.cpp
    pdffireg2p.h
    pdffirenaturalspeech.cpp
    pdffirenaturalspeech.h
)

add_compile_definitions(QT_INSTALL_DIRECTORY="${QT6_INSTALL_PREFIX}")

include(GenerateExportHeader)

GENERATE_EXPORT_HEADER(Pdf4QtLibGui
                       EXPORT_MACRO_NAME
                       PDF4QTLIBGUILIBSHARED_EXPORT
                       EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR}/pdf4qtlibgui_export.h")

target_link_libraries(Pdf4QtLibGui PRIVATE Pdf4QtLibCore Pdf4QtLibWidgets Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Xml Qt6::Svg Qt6::Concurrent Qt6::Multimedia)

# PDF Fire: the natural voices (Kokoro model, Apache 2.0) run on ONNX Runtime (MIT).
# The runtime and the voice data (deps/kokoro) are installed with the program.
set(PDFFIRE_KOKORO_DIR "${CMAKE_SOURCE_DIR}/../deps/kokoro" CACHE PATH "Folder with ONNX Runtime and the voice data")
if(WIN32)
    set(PDFFIRE_ONNXRUNTIME_PLATFORM "win-x64")
else()
    set(PDFFIRE_ONNXRUNTIME_PLATFORM "linux-x64")
endif()
set(PDFFIRE_ONNXRUNTIME_DIR "${PDFFIRE_KOKORO_DIR}/onnxruntime-${PDFFIRE_ONNXRUNTIME_PLATFORM}-1.23.2" CACHE PATH "ONNX Runtime (headers and the library)")
target_include_directories(Pdf4QtLibGui PRIVATE ${PDFFIRE_ONNXRUNTIME_DIR}/include)
if(WIN32)
    target_link_libraries(Pdf4QtLibGui PRIVATE ${PDFFIRE_ONNXRUNTIME_DIR}/lib/onnxruntime.lib)
else()
    target_link_libraries(Pdf4QtLibGui PRIVATE ${PDFFIRE_ONNXRUNTIME_DIR}/lib/libonnxruntime.so)
endif()
target_compile_definitions(Pdf4QtLibGui PRIVATE PDFFIRE_DEV_VOICE_DIR="${PDFFIRE_KOKORO_DIR}/voice")
if(WIN32)
    # The DLLs go next to the program (and into the build folder, for running from there)
    set(PDFFIRE_ONNXRUNTIME_LIBRARIES "${PDFFIRE_ONNXRUNTIME_DIR}/lib/onnxruntime.dll" "${PDFFIRE_ONNXRUNTIME_DIR}/lib/onnxruntime_providers_shared.dll")
    install(FILES ${PDFFIRE_ONNXRUNTIME_LIBRARIES} DESTINATION ${PDF4QT_INSTALL_BIN_DIR})
    file(COPY ${PDFFIRE_ONNXRUNTIME_LIBRARIES} DESTINATION ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})
else()
    file(GLOB PDFFIRE_ONNXRUNTIME_LIBRARIES "${PDFFIRE_ONNXRUNTIME_DIR}/lib/libonnxruntime.so.*")
    install(FILES ${PDFFIRE_ONNXRUNTIME_LIBRARIES} DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
endif()
if(WIN32)
    set(PDFFIRE_VOICE_INSTALL_DIR ${PDF4QT_INSTALL_BIN_DIR})   # <program folder>/voice
else()
    set(PDFFIRE_VOICE_INSTALL_DIR share/pdf-fire)
endif()
install(DIRECTORY ${PDFFIRE_KOKORO_DIR}/voice DESTINATION ${PDFFIRE_VOICE_INSTALL_DIR})
install(FILES ${PDFFIRE_ONNXRUNTIME_DIR}/LICENSE DESTINATION ${PDFFIRE_VOICE_INSTALL_DIR}/voice RENAME LICENSE-onnxruntime.txt)
target_include_directories(Pdf4QtLibGui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(Pdf4QtLibGui PUBLIC ${CMAKE_BINARY_DIR}/${INSTALL_INCLUDEDIR})

set_target_properties(Pdf4QtLibGui PROPERTIES
                      VERSION ${PDF4QT_VERSION}
                      SOVERSION ${PDF4QT_VERSION}
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_LIB_DIR}
                      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${PDF4QT_INSTALL_BIN_DIR})

install(TARGETS Pdf4QtLibGui RUNTIME DESTINATION ${PDF4QT_INSTALL_BIN_DIR} LIBRARY DESTINATION ${PDF4QT_INSTALL_LIB_DIR})
