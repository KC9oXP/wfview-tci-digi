# End-to-end test against a mock wfview TCI server (see test_e2e.cpp).
TEMPLATE = app
TARGET = test_e2e
QT += widgets websockets
CONFIG += c++17 console
CONFIG -= app_bundle
DEFINES += APP_VERSION=\\\"test\\\"
win32 {
    msvc: QMAKE_CXXFLAGS += /utf-8
    DEFINES += _USE_MATH_DEFINES NOMINMAX
}

INCLUDEPATH += $$PWD $$PWD/../src $$PWD/../engine $$PWD/../engine/ggmorse/include

HEADERS += \
    MockTciServer.h \
    ../src/tciprotocol.h \
    ../src/TciClient.h \
    ../src/CwDecoder.h \
    ../src/RttyEngine.h \
    ../src/ScopeWidget.h \
    ../src/CwPanel.h \
    ../src/RttyPanel.h \
    ../src/MainWindow.h

SOURCES += \
    test_e2e.cpp \
    MockTciServer.cpp \
    ../src/tciprotocol.cpp \
    ../src/TciClient.cpp \
    ../src/CwDecoder.cpp \
    ../src/RttyEngine.cpp \
    ../src/ScopeWidget.cpp \
    ../src/CwPanel.cpp \
    ../src/RttyPanel.cpp \
    ../src/MainWindow.cpp \
    ../engine/rttycore.cpp \
    ../engine/rttytxsource.cpp \
    ../engine/ggmorse/src/ggmorse.cpp \
    ../engine/ggmorse/src/resampler.cpp
