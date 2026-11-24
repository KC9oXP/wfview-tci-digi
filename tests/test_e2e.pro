# End-to-end test against a mock wfview TCI server (see test_e2e.cpp).
TEMPLATE = app
TARGET = test_e2e
QT += widgets websockets network
CONFIG += c++17 console
CONFIG -= app_bundle
DEFINES += APP_VERSION=\\\"test\\\"
win32 {
    msvc: QMAKE_CXXFLAGS += /utf-8
    DEFINES += _USE_MATH_DEFINES NOMINMAX
    LIBS += -lshell32 -ladvapi32 -lcrypt32
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
    ../src/RsidEngine.h \
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
    ../src/AppPaths.h \
    ../src/RxText.h \
    ../src/AudioHub.h \
    ../src/SessionLog.h \
    ../src/QsoLog.h \
    ../src/ImageView.h \
    ../src/OliviaEngine.h \
    ../src/OliviaPanel.h \
    ../src/Mt63Engine.h \
    ../src/Mt63Panel.h \
    ../src/NavtexEngine.h \
    ../src/NavtexPanel.h \
    ../src/SstvEngine.h \
    ../src/SstvPanel.h \
    ../src/WefaxEngine.h \
    ../src/WefaxPanel.h \
    ../src/AmtorEngine.h \
    ../src/AmtorPanel.h \
    ../src/FreqEdit.h \
    ../src/TxLevelSlider.h \
    ../src/TxTools.h \
    ../src/RadioModes.h \
    ../src/UiStyle.h \
    ../src/WwvEngine.h \
    ../src/WwvPanel.h \
    ../src/ClockSet.h \
    ../src/UiLanguage.h \
    ../src/UpdateCheck.h \
    ../src/MemoriesDialog.h \
    ../src/PskReporter.h \
    ../src/SecretStore.h \
    ../src/LogUploader.h \
    ../src/GridTrackerLink.h \
    ../src/InputLevelMeter.h \
    ../src/CallLookup.h \
    ../engine/lookupcore.h \
    ../engine/signalreport.h \
    ../engine/uploadcore.h \
    ../engine/gridtrackerudp.h \
    ../engine/inputlevel.h \
    ../engine/stanagegg.h \
    ../engine/callspot.h \
    ../engine/pskrcore.h \
    ../engine/memories.h \
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
    ../src/RsidEngine.cpp \
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
    ../src/AppPaths.cpp \
    ../src/AudioHub.cpp \
    ../src/SessionLog.cpp \
    ../src/QsoLog.cpp \
    ../src/ImageView.cpp \
    ../src/OliviaEngine.cpp \
    ../src/OliviaPanel.cpp \
    ../src/Mt63Engine.cpp \
    ../src/Mt63Panel.cpp \
    ../src/NavtexEngine.cpp \
    ../src/NavtexPanel.cpp \
    ../src/SstvEngine.cpp \
    ../src/SstvPanel.cpp \
    ../src/WefaxEngine.cpp \
    ../src/WefaxPanel.cpp \
    ../src/RxText.cpp \
    ../src/AmtorEngine.cpp \
    ../src/AmtorPanel.cpp \
    ../src/FreqEdit.cpp \
    ../src/TxLevelSlider.cpp \
    ../src/TxTools.cpp \
    ../src/WwvEngine.cpp \
    ../src/WwvPanel.cpp \
    ../src/ClockSet.cpp \
    ../src/UiLanguage.cpp \
    ../src/UpdateCheck.cpp \
    ../src/MemoriesDialog.cpp \
    ../src/PskReporter.cpp \
    ../src/SecretStore.cpp \
    ../src/LogUploader.cpp \
    ../src/GridTrackerLink.cpp \
    ../src/InputLevelMeter.cpp \
    ../src/CallLookup.cpp \
    ../src/MainWindow.cpp \
    ../engine/sstvcore.cpp \
    ../engine/wefaxcore.cpp \
    ../engine/wwvcore.cpp \
    ../engine/navtexcore.cpp \
    ../engine/oliviacore.cpp \
    ../engine/mt63core.cpp \
    ../engine/mt63/dsp.cxx \
    ../engine/mt63/mt63base.cxx \
    ../engine/wavfile.cpp \
    ../engine/rttycore.cpp \
    ../engine/rttytxsource.cpp \
    ../engine/pskcore.cpp \
    ../engine/rsidcore.cpp \
    ../engine/hellcore.cpp \
    ../engine/cwtxcore.cpp \
    ../engine/modemtxsource.cpp \
    ../engine/spectrum.cpp \
    ../engine/ggmorse/src/ggmorse.cpp \
    ../engine/ggmorse/src/resampler.cpp
