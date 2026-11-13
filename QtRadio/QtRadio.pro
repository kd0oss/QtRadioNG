#-------------------------------------------------
#
# Project created by QtCreator 2010-07-05T10:00:09
# QtRadioNG requires QT 6
#-------------------------------------------------

lessThan(QT_MAJOR_VERSION, 6): error("QtRadio requires Qt 6 (found $$QT_VERSION)")
 
QT       += core gui widgets network multimedia

#KD0NUZ OSX Homebrew includes/libs
INCLUDEPATH += /usr/local/include
LIBS += -L/usr/local/lib

QMAKE_CXXFLAGS_WARN_ON += -Wno-unused-parameter
QMAKE_CXXFLAGS += -fopenmp

SOURCES += main.cpp\
    UI.cpp \
    Mode.cpp \
    Filters.cpp \
    Connection.cpp \
    Configure.cpp \
    BandStackEntry.cpp \
    Band.cpp \
    Audio.cpp \
    BandLimit.cpp \
    FrequencyInfo.cpp \
    Frequency.cpp \
    Meter.cpp \
    Bandscope.cpp \
    About.cpp \
    Buffer.cpp \
    Bookmark.cpp \
    BookmarkDialog.cpp \
    BookmarksEditDialog.cpp \
    Xvtr.cpp \
    XvtrEntry.cpp \
    Bookmarks.cpp \
    KeypadDialog.cpp \
    hackrfoneframe.cpp \
    flexframe.cpp \
    pluginframe.cpp \
    pluginregistry.cpp \
    sdr1000frame.cpp \
    smeter.cpp \
    rigctl.cpp \
    vfo.cpp \
    ctl.cpp \
    Audioinput.cpp\
    servers.cpp \
    G711A.cpp \
    calc.cpp \
    EqualizerDialog.cpp \
    Panadapter.cpp \
    hermesframe.cpp \
    radiosdialog.cpp


HEADERS  += \
    hackrfoneframe.h \
    flexframe.h \
    pluginframe.h \
    pluginregistry.h \
    sdr1000frame.h\
    UI.h \
    Mode.h \
    Filters.h \
    Connection.h \
    Configure.h \
    BandStackEntry.h \
    Band.h \
    Audio.h \
    BandLimit.h \
    FrequencyInfo.h \
    Frequency.h \
    Meter.h \
    Bandscope.h \
    About.h \
    Buffer.h \
    Bookmark.h \
    BookmarkDialog.h \
    BookmarksEditDialog.h \
    Xvtr.h \
    XvtrEntry.h \
    Bookmarks.h \
    KeypadDialog.h \
    smeter.h \
    rigctl.h \
    vfo.h \
    ctl.h \
    Audioinput.h \
    servers.h \
    G711A.h \
    cusdr_queue.h \
    calc.h \
    EqualizerDialog.h \
    Panadapter.h \
    hermesframe.h \
    radiosdialog.h

FORMS    += \
    hackrfoneframe.ui \
    flexframe.ui \
    sdr1000frame.ui \
    UI.ui \
    Configure.ui \
    Bandscope.ui \
    About.ui \
    Bookmark.ui \
    BookmarksEditDialog.ui \
    KeypadDialog.ui \
    vfo.ui \
    ctl.ui \
    servers.ui \
    EqualizerDialog.ui \
    hermesframe.ui \
    radiosdialog.ui

LIBS += -lsamplerate -lgomp
