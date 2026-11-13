#ifndef BOOKMARKDIALOG_H
#define BOOKMARKDIALOG_H

#include <QtCore>

#include <QFrame>
#include <QDialog>
#include "ui_Bookmark.h"

class BookmarkDialog : public QDialog {
    Q_OBJECT
public:
    BookmarkDialog();

    void setTitle(QString t);
    void setFrequency(QString f);

    QString getTitle();

private:
    Ui::BookmarkDialog widget;
};

#endif // BOOKMARKDIALOG_H
