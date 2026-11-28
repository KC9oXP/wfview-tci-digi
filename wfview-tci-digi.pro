# wfview TCI Digi - stand-alone digital modes for wfview over TCI: CW, RTTY, PSK,
# Feld Hell, Olivia/Contestia, MT63, SSTV, AMTOR FEC, WSPR, NAVTEX and WEFAX.
# Qt 6 (Widgets, WebSockets, Network), C++17.

TEMPLATE = app
TARGET = wfview-tci-digi
QT += widgets websockets network
CONFIG += c++17
CONFIG -= console

VERSION = 0.9.21
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

win32 {
    QMAKE_TARGET_COMPANY = KC9oXP
    QMAKE_TARGET_PRODUCT = "wfview TCI Digi"
    QMAKE_TARGET_DESCRIPTION = "Digital modes for wfview over TCI"
    RC_ICONS = icons/wfview-tci-digi.ico
    msvc: QMAKE_CXXFLAGS += /utf-8
    DEFINES += _USE_MATH_DEFINES NOMINMAX
    LIBS += -lshell32 -ladvapi32 -lcrypt32
}

INCLUDEPATH += src engine engine/ggmorse/include
RESOURCES += wfview-tci-digi.qrc
macx: ICON = icons/wfview-tci-digi.icns

HEADERS += \
    src/UiLanguage.h \
    src/tciprotocol.h \
    src/TciClient.h \
    src/CwDecoder.h \
    src/RttyEngine.h \
    src/ScopeWidget.h \
    src/CwPanel.h \
    src/RttyPanel.h \
    src/ModemEngine.h \
    src/PskEngine.h \
    src/RsidEngine.h \
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
    src/AppPaths.h \
    src/RxText.h \
    src/AudioHub.h \
    src/SessionLog.h \
    src/QsoLog.h \
    src/ImageView.h \
    src/OliviaEngine.h \
    src/OliviaPanel.h \
    src/Mt63Engine.h \
    src/Mt63Panel.h \
    src/NavtexEngine.h \
    src/NavtexPanel.h \
    src/SstvEngine.h \
    src/SstvPanel.h \
    src/WefaxEngine.h \
    src/WefaxPanel.h \
    src/AmtorEngine.h \
    src/AmtorPanel.h \
    src/FreqEdit.h \
    src/TxLevelSlider.h \
    src/TxTools.h \
    src/RadioModes.h \
    src/UiStyle.h \
    src/WwvEngine.h \
    src/WwvPanel.h \
    src/ClockSet.h \
    src/UpdateCheck.h \
    src/MemoriesDialog.h \
    src/PskReporter.h \
    src/SecretStore.h \
    src/LogUploader.h \
    src/GridTrackerLink.h \
    src/InputLevelMeter.h \
    src/SquelchControl.h \
    src/SquelchMeter.h \
    src/SquelchSetting.h \
    src/CallLookup.h \
    src/WsprEngine.h \
    src/WsprPanel.h \
    src/WsprNet.h \
    engine/wsprcore.h \
    engine/wsprtxsource.h \
    engine/wsprd/fano.h \
    engine/wsprd/metric_tables.h \
    engine/wsprd/nhash.h \
    engine/wsprd/wsprd.h \
    engine/wsprd/wsprd_utils.h \
    engine/wsprd/wsprsim_utils.h \
    engine/lookupcore.h \
    engine/signalreport.h \
    engine/rstparse.h \
    engine/uploadcore.h \
    engine/gridtrackerudp.h \
    engine/inputlevel.h \
    engine/stanagegg.h \
    engine/callspot.h \
    engine/pskrcore.h \
    src/MainWindow.h \
    engine/memories.h \
    engine/audioresampler.h \
    engine/fmdemod.h \
    engine/sstvboxes.h \
    engine/sstvcore.h \
    engine/wefaxcore.h \
    engine/navtexcore.h \
    engine/oliviacore.h \
    engine/mt63core.h \
    engine/extratxsources.h \
    engine/wavfile.h \
    engine/versioncheck.h \
    engine/txlevel.h \
    engine/autolevel.h \
    engine/freqdigits.h \
    engine/jalocha/pj_mfsk.h \
    engine/cwdecodercore.h \
    engine/cwhandkeydecoder.h \
    engine/cwmultidecoder.h \
    engine/cwwordjoiner.h \
    engine/rttycore.h \
    engine/squelch.h \
    engine/rttytxsource.h \
    engine/pskcore.h \
    engine/pskmultidecoder.h \
    engine/rsidcore.h \
    engine/sidebandfix.h \
    engine/hellcore.h \
    engine/cwtxcore.h \
    engine/modemtxsource.h \
    engine/spectrum.h \
    engine/ggmorse/include/ggmorse/ggmorse.h

SOURCES += \
    src/UiLanguage.cpp \
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
    src/RsidEngine.cpp \
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
    src/AppPaths.cpp \
    src/AudioHub.cpp \
    src/SessionLog.cpp \
    src/QsoLog.cpp \
    src/ImageView.cpp \
    src/OliviaEngine.cpp \
    src/OliviaPanel.cpp \
    src/Mt63Engine.cpp \
    src/Mt63Panel.cpp \
    src/NavtexEngine.cpp \
    src/NavtexPanel.cpp \
    src/SstvEngine.cpp \
    src/SstvPanel.cpp \
    src/WefaxEngine.cpp \
    src/WefaxPanel.cpp \
    src/RxText.cpp \
    src/AmtorEngine.cpp \
    src/AmtorPanel.cpp \
    src/FreqEdit.cpp \
    src/TxLevelSlider.cpp \
    src/TxTools.cpp \
    src/WwvEngine.cpp \
    src/WwvPanel.cpp \
    src/ClockSet.cpp \
    src/UpdateCheck.cpp \
    src/MemoriesDialog.cpp \
    src/PskReporter.cpp \
    src/SecretStore.cpp \
    src/LogUploader.cpp \
    src/GridTrackerLink.cpp \
    src/InputLevelMeter.cpp \
    src/SquelchControl.cpp \
    src/SquelchMeter.cpp \
    src/CallLookup.cpp \
    src/WsprEngine.cpp \
    src/WsprPanel.cpp \
    src/WsprNet.cpp \
    engine/wsprcore.cpp \
    engine/wsprd/fano.cpp \
    engine/wsprd/nhash.cpp \
    engine/wsprd/tab.cpp \
    engine/wsprd/wsprd.cpp \
    engine/wsprd/wsprd_utils.cpp \
    engine/wsprd/wsprsim_utils.cpp \
    src/MainWindow.cpp \
    engine/sstvcore.cpp \
    engine/wefaxcore.cpp \
    engine/wwvcore.cpp \
    engine/navtexcore.cpp \
    engine/oliviacore.cpp \
    engine/mt63core.cpp \
    engine/mt63/dsp.cxx \
    engine/mt63/mt63base.cxx \
    engine/wavfile.cpp \
    engine/rttycore.cpp \
    engine/rttytxsource.cpp \
    engine/pskcore.cpp \
    engine/pskmultidecoder.cpp \
    engine/rsidcore.cpp \
    engine/hellcore.cpp \
    engine/cwtxcore.cpp \
    engine/modemtxsource.cpp \
    engine/spectrum.cpp \
    engine/ggmorse/src/ggmorse.cpp \
    engine/ggmorse/src/resampler.cpp

# Qt Linguist translations (Options > Language). The .ts catalogs are compiled
# with lrelease at build time and built into the executable under :/i18n, so the
# Windows zip and the Linux AppImage carry them with nothing extra to deploy.
# A .qm placed in a "translations" folder beside the executable is also picked
# up (and wins over the built-in one of the same name). See translations/README.md.
TRANSLATIONS += \
    translations/wfview_tci_digi_ja_JP.ts

# lrelease ships with Qt (Qt Linguist tools). Without it the app still builds,
# in English only.
exists($$[QT_HOST_BINS]/lrelease*)|exists($$[QT_INSTALL_BINS]/lrelease*) {
    CONFIG += lrelease embed_translations
    QM_FILES_RESOURCE_PREFIX = /i18n
} else {
    warning("lrelease not found in Qt's bin folder: building without translations")
}
