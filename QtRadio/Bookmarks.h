#ifndef BOOKMARKS_H
#define BOOKMARKS_H

#include <QObject>
#include <QVector>
#include <QSettings>

#include <QMenu>

#include "Bookmark.h"
#include "Bookmarks.h"

class Bookmarks: public QObject {
    Q_OBJECT
public:
    Bookmarks();
    void saveSettings(QSettings* settings);
    void loadSettings(QSettings* settings);
    void buildMenu(QMenu* menu);
    void add(Bookmark* bookmark);
    void remove(int index);
    Bookmark* at(int index);
    int count();
    void select(QAction* action);
    bool hasSelection(void);
    int getBand();
    long long getFrequency();
    int getMode();
    int getFilter();
public slots:
    void triggered();
signals:
    void bookmarkSelected(QAction*);
private:
    void addPresets(void);
    QVector<Bookmark*> bookmarks;
    QVector<Bookmark*> cbChannels;     // built-in, never saved or edited
    Bookmark* currentBookmark;
};

#endif // BOOKMARKS_H
