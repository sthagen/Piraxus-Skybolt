cmake_minimum_required(VERSION 3.15)
project(cmake_wrapper)

macro(custom_find_package name variable)
    string(TOUPPER ${name} name_upper)
    if(${variable})
        find_package(${name} ${ARGN} REQUIRED CONFIG
            # Allow only Conan packages
            NO_DEFAULT_PATH
            PATHS ${CMAKE_PREFIX_PATH}
        )
        set(${name_upper}_FOUND TRUE)
        set(${name_upper}_VERSION_STRING ${${name}_VERSION_STRING})
        set(${name_upper}_INCLUDE_DIRS ${${name}_INCLUDE_DIRS})
        set(${name_upper}_INCLUDE_DIR ${${name}_INCLUDE_DIR})
        set(${name_upper}_LIBRARIES ${${name}_LIBRARIES})
        set(${name_upper}_DEFINITIONS ${${name}_DEFINITIONS})
        unset(name_upper)
    else()
        set(${name}_FOUND FALSE)
        set(${name_upper}_FOUND FALSE)
    endif()
endmacro()

custom_find_package(Boost OSG_WITH_ASIO)
custom_find_package(Asio OSG_WITH_ASIO)
custom_find_package(CURL OSG_WITH_CURL)
custom_find_package(Fontconfig OSG_TEXT_USE_FONTCONFIG)
custom_find_package(Freetype OSG_WITH_FREETYPE)
custom_find_package(GDAL OSG_WITH_GDAL)
custom_find_package(GIFLIB OSG_WITH_GIFLIB)
custom_find_package(GTA OSG_WITH_GTA)
custom_find_package(JPEG OSG_WITH_JPEG)
custom_find_package(Jasper OSG_WITH_JASPER)
custom_find_package(OpenEXR OSG_WITH_OPENEXR)
custom_find_package(PNG OSG_WITH_PNG)
custom_find_package(TIFF OSG_WITH_TIFF)
custom_find_package(ZLIB OSG_WITH_ZLIB)
custom_find_package(DCMTK OSG_WITH_DCMTK)
# custom_find_package(Poppler)
# custom_find_package(RSVG)
# custom_find_package(SDL2)

add_subdirectory(src)