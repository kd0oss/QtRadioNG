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
    int getBand();
    long long getFrequency();
    int getMode();
    int getFilter();
public slots:
    void triggered();
signals:
    void bookmarkSelected(QAction*);
private:
    QVector<Bookmark*> bookmarks;
    Bookmark* currentBookmark;
};

#endif // BOOKMARKS_H
