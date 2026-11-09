# SPDX-License-Identifier: GPL-3.0-or-later

set(LICHII_TGM_PRODUCT_NAME "Lichii-tgm")
set(LICHII_TGM_VERSION "1.0.0")
set(LICHII_TGM_RELEASE_TAG "Lichii-tgm-v1.0")
set(LICHII_TGM_REPOSITORY "ThatGuyMcd/Lichii-TGM")
set(LICHII_TGM_APP_ID "io.github.thatguymcd.lichii_tgm")
string(REPLACE "." "," LICHII_TGM_WINDOWS_VERSION "${LICHII_TGM_VERSION}.0")

foreach(_metadata desktop metainfo.xml xml)
    configure_file("${CMAKE_SOURCE_DIR}/dist/${LICHII_TGM_APP_ID}.${_metadata}.in"
                   "${CMAKE_BINARY_DIR}/dist/${LICHII_TGM_APP_ID}.${_metadata}" @ONLY)
endforeach()

if(WIN32)
    configure_file("${CMAKE_SOURCE_DIR}/dist/lichii-tgm-version.rc.in"
                   "${CMAKE_BINARY_DIR}/dist/lichii-tgm-version.rc" @ONLY)
endif()
