# openhantek/CMakeLists.txt

project(OpenHantek CXX)

# Este fork (DSO-5200/5200A) solo se compila en Linux: la compilación para Windows del upstream
# dependía de los drivers y DLL del 6022, que se quitaron.
if(WIN32)
    message(FATAL_ERROR "dso5200-openhantek solo se compila en Linux")
endif()


find_package(Qt6PrintSupport REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)
find_package(OpenGL)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# After Qt5 -> Qt6 transition use minimum Qt 6.2 LTS stadard
if (Qt6Widgets_VERSION VERSION_LESS 6.2.0)
    message(FATAL_ERROR "Minimum supported Qt6 version is 6.2.0!")
else()
    message("-- Qt6 version is ${Qt6Widgets_VERSION}")
endif()

# include directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(src/ src/hantekdso src/widgets src/docks src/configdialog)

# collect sources and other files
file(GLOB_RECURSE SRC "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")
file(GLOB_RECURSE UI "src/*.ui")
file(GLOB_RECURSE QRC "res/*.qrc")

if(WIN32)
    file(GLOB_RECURSE RC "res/*.rc")
endif()

add_custom_target(format SOURCES ".clang-format"
    COMMAND "clang-format" "-style=file" "-i" "-sort-includes" ${SRC} ${HEADERS})

# here the translation magic happens
include(translations/Translations.cmake)

option(USE_OPENHANTEK_DRIVER "Use OpenHantek Windows driver" ON)

# Enable MacOSX bundle magic in the next line
option(BUILD_MACOSX_BUNDLE "Build MacOS app bundle" ON)
#
if( APPLE AND BUILD_MACOSX_BUNDLE )
    set( EXECTYPE MACOSX_BUNDLE )
    set( MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}" )
    set( MACOSX_BUNDLE_INFO_STRING "Digital oscilloscope software for Hantek DSO-5200/5200A" )
    set( MACOSX_BUNDLE_GUI_IDENTIFIER "org.openhantek.${PROJECT_NAME}" )
    set( MACOSX_BUNDLE_SHORT_VERSION_STRING ${VCS_REVISION} )
    set( MACOSX_BUNDLE_BUNDLE_VERSION ${VERSION} )
    string(TIMESTAMP THISYEAR "%Y")
    set( MACOSX_BUNDLE_COPYRIGHT "(C) 2012-${THISYEAR} OpenHantek community" )
    set( MACOSX_BUNDLE_ICON_FILE openhantek.icns )
    set( ICONS ${CMAKE_CURRENT_SOURCE_DIR}/res/images/openhantek.icns )
    set_source_files_properties( ${ICONS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources" )
endif()

# make executable
add_executable(${PROJECT_NAME} ${EXECTYPE} ${SRC} ${HEADERS} ${UI}
${QRC} ${RC} ${TRANSLATION_BIN_FILES} ${TRANSLATION_QRC} ${ICONS})
target_link_libraries(${PROJECT_NAME} Qt6::Widgets Qt6::PrintSupport Qt6::OpenGL Qt6::OpenGLWidgets ${OPENGL_LIBRARIES} )
if( APPLE AND BUILD_MACOSX_BUNDLE )
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(${PROJECT_NAME} Qt6::DBus )
endif()
target_compile_features(${PROJECT_NAME} PRIVATE cxx_range_for)

if(TRUE)
    if( APPLE AND BUILD_MACOSX_BUNDLE )
        # Use own template that defines NSPrincipalClass=NSApplication & NSHighResolutionCapable=True
        set_target_properties( ${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenHantekBundleInfo.plist.in
        )
    endif()
    find_package(libusb REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${LIBUSB_LIBRARIES})

    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} ${CMAKE_THREAD_LIBS_INIT})

    find_package(FFTW REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFTW_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${FFTW_LIBRARIES})

    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:-DDEBUG>")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:-O0>")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:RELEASE>:-fno-rtti>")
endif()

# --- Núcleo del protocolo DSO-5200/5200A -------------------------------------------------------
# Las fuentes del protocolo se compilan TAL CUAL desde dso5200basic (repo eamadosuarez83/dso5200-cli),
# que es la referencia validada contra el aparato y no se modifica desde aquí. Por omisión se espera
# como carpeta hermana de este repo; se puede apuntar a otra copia con -DDSO5200BASIC_DIR=...
set(DSO5200BASIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dso5200basic" CACHE PATH "Raíz de dso5200basic")
if(NOT EXISTS "${DSO5200BASIC_DIR}/src/device/dso5200.cpp")
    message(FATAL_ERROR "No se encuentra dso5200basic en '${DSO5200BASIC_DIR}' (usa -DDSO5200BASIC_DIR=...)")
endif()
add_library(dso5200core STATIC
    dso5200core/dso5200core.cpp
    ${DSO5200BASIC_DIR}/src/device/dso5200.cpp
    ${DSO5200BASIC_DIR}/src/device/simulator.cpp
    ${DSO5200BASIC_DIR}/src/usb/usbdevice.cpp
    ${DSO5200BASIC_DIR}/src/protocol/bulkcommand.cpp
    ${DSO5200BASIC_DIR}/src/protocol/bulkStructs.cpp
    ${DSO5200BASIC_DIR}/src/protocol/controlcommand.cpp
    ${DSO5200BASIC_DIR}/src/protocol/controlStructs.cpp)
# BEFORE: los includes de dso5200basic ("usb/usbdevice.h", "protocol/...") deben resolverse en su
# árbol antes que en src/ de la GUI, que se hereda de include_directories() más arriba.
target_include_directories(dso5200core BEFORE PRIVATE ${DSO5200BASIC_DIR}/src)
set_target_properties(dso5200core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(NOT MSVC)
    # dso5200basic se escribió para C++17; con el estándar de la GUI avisa de operaciones entre enums
    # de libusb que son correctas. Se silencia solo en el núcleo.
    target_compile_options(dso5200core PRIVATE -Wno-deprecated-enum-enum-conversion)
endif()
# La GUI define una `libUsbErrorString(int)` global que solo difiere en el tipo de retorno de la del
# núcleo: se renombra en el núcleo para que no choquen al enlazar. (`Hantek` se renombraba porque el
# protocolo del 6022 usaba el mismo namespace; ya no existe, se deja por si acaso.)
target_compile_definitions(dso5200core PRIVATE Hantek=Hantek5200core libUsbErrorString=dso5200core_libUsbErrorString)
if(NOT MSVC)
    target_include_directories(dso5200core PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_libraries(dso5200core PUBLIC ${LIBUSB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()
target_include_directories(${PROJECT_NAME} PRIVATE dso5200core)
target_link_libraries(${PROJECT_NAME} dso5200core)

# install commands
if ( NOT (APPLE AND BUILD_MACOSX_BUNDLE) )
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION "bin")
endif()
