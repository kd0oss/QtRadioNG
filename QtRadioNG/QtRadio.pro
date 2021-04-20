#-------------------------------------------------
#
# Project created by QtCreator 2010-07-05T10:00:09
#
#-------------------------------------------------



#-------------------------------------------------
# If using the Nokia Qt SDK set _UsingSDK to true
# or if using Ubuntu repo set _UsingSDK to false
#-------------------------------------------------

_UsingSDK = false

greaterThan(QT_MAJOR_VERSION, 4) {
    message("Using Qt5")
    QT       += core gui widgets network multimedia


#    INCLUDEPATH += /opt/qt5/include
#    INCLUDEPATH += /opt/qt5/include/QtMultimedia
#    INCLUDEPATH += /opt/qt5/include/QtNetwork

    #KD0NUZ OSX Homebrew includes/libs
    INCLUDEPATH += /usr/local/include
    LIBS += -L/usr/local/lib

#    INCLUDEPATH += /usr/local/Qt-5.0.2/include/QtCore
#    INCLUDEPATH += /usr/local/Qt-5.0.2/include/QtGui
#    INCLUDEPATH += /usr/local/Qt-5.0.2/include/QtWidgets
#    INCLUDEPATH += /usr/local/Qt-5.0.2/include/QtMultimedia
} else {
    $$_UsingSDK {
        message("Using the Nokia Qt SDK installation")
        QT       += core gui network multimedia mobility
        CONFIG   += mobility
        MOBILITY += multimedia
    } else {
        message("Using the Ubuntu Qt Creator installation")
        CONFIG   += mobility
        MOBILITY += multimedia
        INCLUDEPATH += /usr/include/QtMobility
        INCLUDEPATH += /usr/include/QtMultimediaKit
    }
}

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
    BookmarksDialog.cpp \
    BookmarksEditDialog.cpp \
    Xvtr.cpp \
    XvtrEntry.cpp \
    Bookmarks.cpp \
    KeypadDialog.cpp \
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
    BookmarksDialog.h \
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
    UI.ui \
    Configure.ui \
    Bandscope.ui \
    About.ui \
    Bookmark.ui \
    BookmarksDialog.ui \
    BookmarksEditDialog.ui \
    KeypadDialog.ui \
    vfo.ui \
    ctl.ui \
    servers.ui \
    EqualizerDialog.ui \
    hermesframe.ui \
    radiosdialog.ui

OTHER_FILES += \
    android/AndroidManifest.xml \
    android/version.xml \
    android/src/org/qtproject/qt5/android/bindings/QtActivity.java \
    android/src/org/qtproject/qt5/android/bindings/QtApplication.java \
    android/src/org/kde/necessitas/ministro/IMinistro.aidl \
    android/src/org/kde/necessitas/ministro/IMinistroCallback.aidl \
    android/res/values-es/strings.xml \
    android/res/values-rs/strings.xml \
    android/res/values-ms/strings.xml \
    android/res/values-it/strings.xml \
    android/res/values-id/strings.xml \
    android/res/values-de/strings.xml \
    android/res/layout/splash.xml \
    android/res/values-et/strings.xml \
    android/res/values-zh-rCN/strings.xml \
    android/res/values-fr/strings.xml \
    android/res/values-ro/strings.xml \
    android/res/values-ru/strings.xml \
    android/res/values-zh-rTW/strings.xml \
    android/res/values-pl/strings.xml \
    android/res/values-pt-rBR/strings.xml \
    android/res/values/strings.xml \
    android/res/values/libs.xml \
    android/res/values-fa/strings.xml \
    android/res/values-nl/strings.xml \
    android/res/values-ja/strings.xml \
    android/res/values-el/strings.xml \
    android/res/values-nb/strings.xml

win32:CONFIG(release, debug|release): LIBS += -lsamplerate -lgomp
else:win32:CONFIG(debug, debug|release): LIBS += -lsamplerate -lgomp
#else:symbian: LIBS += -lcodec2 -lsamplerate
#else:unix: LIBS += -lcodec2 -lsamplerate -lortp
else:symbian: LIBS += -lsamplerate
else:unix: LIBS += -lsamplerate -lgomp
