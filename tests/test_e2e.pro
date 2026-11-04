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
RESOURCES += ../wfview-tci-digi.qrc

HEADERS += \
    MockTciServer.h \
    ../src/tciprotocol.h \
    ../src/TciClient.h \
    ../src/CwDecoder.h \
    ../src/RttyEngine.h \
    ../src/ScopeWidget.h \
    ../src/CwPanel.h \
    ../src/RttyPanel.h \
    ../src/ModemEngine.h \
    ../src/PskEngine.h \
    ../src/HellEngine.h \
    ../src/SpectrumEngine.h \
    ../src/WaterfallWidget.h \
    ../src/HellWidget.h \
    ../src/ModemPanel.h \
    ../src/ModemTxBox.h \
    ../src/CwTxEngine.h \
    ../src/TxPowerWatch.h \
    ../src/PskPanel.h \
    ../src/HellPanel.h \
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
    ../src/ModemEngine.cpp \
    ../src/PskEngine.cpp \
    ../src/HellEngine.cpp \
    ../src/SpectrumEngine.cpp \
    ../src/WaterfallWidget.cpp \
    ../src/HellWidget.cpp \
    ../src/ModemPanel.cpp \
    ../src/ModemTxBox.cpp \
    ../src/CwTxEngine.cpp \
    ../src/TxPowerWatch.cpp \
    ../src/PskPanel.cpp \
    ../src/HellPanel.cpp \
    ../src/MainWindow.cpp \
    ../engine/rttycore.cpp \
    ../engine/rttytxsource.cpp \
    ../engine/pskcore.cpp \
    ../engine/hellcore.cpp \
    ../engine/cwtxcore.cpp \
    ../engine/modemtxsource.cpp \
    ../engine/spectrum.cpp \
    ../engine/ggmorse/src/ggmorse.cpp \
    ../engine/ggmorse/src/resampler.cpp
