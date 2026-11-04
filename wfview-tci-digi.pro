# wfview TCI Digi - stand-alone CW, RTTY, PSK and Feld Hell for wfview over TCI.
# Qt 6 (Widgets, WebSockets), C++17.

TEMPLATE = app
TARGET = wfview-tci-digi
QT += widgets websockets
CONFIG += c++17
CONFIG -= console

VERSION = 0.3.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

win32 {
    QMAKE_TARGET_COMPANY = KC9oXP
    QMAKE_TARGET_PRODUCT = "wfview TCI Digi"
    QMAKE_TARGET_DESCRIPTION = "CW, RTTY, PSK and Feld Hell for wfview over TCI"
    RC_ICONS = icons/wfview-tci-digi.ico
    msvc: QMAKE_CXXFLAGS += /utf-8
    DEFINES += _USE_MATH_DEFINES NOMINMAX
}

INCLUDEPATH += src engine engine/ggmorse/include
RESOURCES += wfview-tci-digi.qrc
macx: ICON = icons/wfview-tci-digi.icns

HEADERS += \
    src/tciprotocol.h \
    src/TciClient.h \
    src/CwDecoder.h \
    src/RttyEngine.h \
    src/ScopeWidget.h \
    src/CwPanel.h \
    src/RttyPanel.h \
    src/ModemEngine.h \
    src/PskEngine.h \
    src/HellEngine.h \
    src/SpectrumEngine.h \
    src/WaterfallWidget.h \
    src/HellWidget.h \
    src/ModemPanel.h \
    src/ModemTxBox.h \
    src/CwTxEngine.h \
    src/TxPowerWatch.h \
    src/PskPanel.h \
    src/HellPanel.h \
    src/MainWindow.h \
    engine/cwdecodercore.h \
    engine/cwhandkeydecoder.h \
    engine/cwmultidecoder.h \
    engine/cwwordjoiner.h \
    engine/rttycore.h \
    engine/rttytxsource.h \
    engine/pskcore.h \
    engine/hellcore.h \
    engine/cwtxcore.h \
    engine/modemtxsource.h \
    engine/spectrum.h \
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
    src/ModemEngine.cpp \
    src/PskEngine.cpp \
    src/HellEngine.cpp \
    src/SpectrumEngine.cpp \
    src/WaterfallWidget.cpp \
    src/HellWidget.cpp \
    src/ModemPanel.cpp \
    src/ModemTxBox.cpp \
    src/CwTxEngine.cpp \
    src/TxPowerWatch.cpp \
    src/PskPanel.cpp \
    src/HellPanel.cpp \
    src/MainWindow.cpp \
    engine/rttycore.cpp \
    engine/rttytxsource.cpp \
    engine/pskcore.cpp \
    engine/hellcore.cpp \
    engine/cwtxcore.cpp \
    engine/modemtxsource.cpp \
    engine/spectrum.cpp \
    engine/ggmorse/src/ggmorse.cpp \
    engine/ggmorse/src/resampler.cpp
