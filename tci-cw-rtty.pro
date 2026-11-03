# TCI CW/RTTY - stand-alone CW decoder and RTTY modem for wfview over TCI.
# Qt 6 (Widgets, WebSockets), C++17.

TEMPLATE = app
TARGET = tci-cw-rtty
QT += widgets websockets
CONFIG += c++17
CONFIG -= console

VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

win32 {
    QMAKE_TARGET_COMPANY = KC9oXP
    QMAKE_TARGET_PRODUCT = "TCI CW/RTTY"
    QMAKE_TARGET_DESCRIPTION = "CW decoder and RTTY modem for wfview over TCI"
    msvc: QMAKE_CXXFLAGS += /utf-8
    DEFINES += _USE_MATH_DEFINES NOMINMAX
}

INCLUDEPATH += src engine engine/ggmorse/include

HEADERS += \
    src/tciprotocol.h \
    src/TciClient.h \
    src/CwDecoder.h \
    src/RttyEngine.h \
    src/ScopeWidget.h \
    src/CwPanel.h \
    src/RttyPanel.h \
    src/MainWindow.h \
    engine/cwdecodercore.h \
    engine/cwhandkeydecoder.h \
    engine/cwmultidecoder.h \
    engine/cwwordjoiner.h \
    engine/rttycore.h \
    engine/rttytxsource.h \
    engine/ggmorse/include/ggmorse/ggmorse.h

SOURCES += \
    src/main.cpp \
    src/tciprotocol.cpp \
    src/TciClient.cpp \
    src/CwDecoder.cpp \
    src/RttyEngine.cpp \
    src/ScopeWidget.cpp \
    src/CwPanel.cpp \
    src/RttyPanel.cpp \
    src/MainWindow.cpp \
    engine/rttycore.cpp \
    engine/rttytxsource.cpp \
    engine/ggmorse/src/ggmorse.cpp \
    engine/ggmorse/src/resampler.cpp
