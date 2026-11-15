#include <QDebug>

#include <QtCore>
#include <QAction>
#include <QMenu>

#include "Bookmarks.h"
#include "Band.h"
#include "Mode.h"

// The 40 Citizens Band channels (FCC 47 CFR 95.963; the same frequencies
// are used for CEPT 40-channel CB). Channels 23-25 are out of order.
static const long long cbChannelHz[40] =
{
    26965000, 26975000, 26985000, 27005000, 27015000,   //  1 -  5
    27025000, 27035000, 27055000, 27065000, 27075000,   //  6 - 10
    27085000, 27105000, 27115000, 27125000, 27135000,   // 11 - 15
    27155000, 27165000, 27175000, 27185000, 27205000,   // 16 - 20
    27215000, 27225000, 27255000, 27235000, 27245000,   // 21 - 25
    27265000, 27275000, 27285000, 27295000, 27305000,   // 26 - 30
    27315000, 27325000, 27335000, 27345000, 27355000,   // 31 - 35
    27365000, 27375000, 27385000, 27395000, 27405000    // 36 - 40
};
#define CB_FILTER_AM_8K 3   // AMFilters index of the 8k filter (10 kHz spacing)


Bookmarks::Bookmarks() {
    currentBookmark=NULL;
    addPresets();
}

// Pre-assigned bookmarks: CB channels 1-40, AM, in their own submenu.
void Bookmarks::addPresets(void)
{
    for (int ch = 1; ch <= 40; ch++)
    {
        Bookmark *b = new Bookmark();
        long long hz = cbChannelHz[ch - 1];

        b->setTitle(QString("CB %1  (%2 MHz)").arg(ch, 2, 10, QChar('0')).arg(hz / 1e6, 0, 'f', 3));
        b->setBand(BAND_GEN);
        b->setFrequency(hz);
        b->setMode(MODE_AM);
        b->setFilter(CB_FILTER_AM_8K);
        cbChannels.append(b);
    }
}

// SLOTS
void Bookmarks::triggered() {
    QAction *action = qobject_cast<QAction *>(sender());
    qDebug()<<"Bookmarks::triggered"<<action->text();
    emit bookmarkSelected(action);
}

void Bookmarks::buildMenu(QMenu* menu) {
    Bookmark* bookmark;
    QAction* action;

    qDebug()<<"Bookmarks::buildMenu"<<bookmarks.size();
    menu->clear();
    for(int i=0;i<bookmarks.size();++i) {
        bookmark=bookmarks.at(i);
        action=new QAction(bookmark->getTitle(),this);
        menu->addAction(action);

        //need an event for the new action
        connect(action,&QAction::triggered,this,&Bookmarks::triggered);

        qDebug()<<"  added "<<action->text();
    }

    if (!bookmarks.isEmpty())
        menu->addSeparator();
    QMenu *cb = menu->addMenu("CB Channels");
    for (int i = 0; i < cbChannels.size(); ++i)
    {
        action = new QAction(cbChannels.at(i)->getTitle(), this);
        cb->addAction(action);
        connect(action, &QAction::triggered, this, &Bookmarks::triggered);
    }
}

void Bookmarks::saveSettings(QSettings* settings) {
    QString s;
    Bookmark* bookmark;
    qDebug()<<"Bookmarks::saveSettings";
    settings->beginGroup("Bookmarks");
    settings->setValue("entries",bookmarks.count());
    for(int i=0;i<bookmarks.count();i++) {
        bookmark=bookmarks.at(i);
        s = QString("title.%1").arg(i);
        settings->setValue(s,bookmark->getTitle());
        s = QString("band.%1").arg(i);
        settings->setValue(s,bookmark->getBand());
        s = QString("frequency.%1").arg(i);
        settings->setValue(s,bookmark->getFrequency());
        s = QString("mode.%1").arg(i);
        settings->setValue(s,bookmark->getMode());
        s = QString("filter.%1").arg(i);
        settings->setValue(s,bookmark->getFilter());
    }
    settings->endGroup();
}

void Bookmarks::loadSettings(QSettings* settings) {
    QString s;
    Bookmark* bookmark;

    qDebug()<<"Bookmarks::loadSettings";
    settings->beginGroup("Bookmarks");
    if(settings->contains("entries")) {
        int entries=settings->value("entries").toInt();
        qDebug()<<" entries="<<entries;
        for(int i=0;i<entries;i++) {
            s = QString("title.%1").arg(i);
            if(settings->contains(s)) {
                bookmark=new Bookmark();
                bookmark->setTitle(settings->value(s).toString());
                s = QString("band.%1").arg(i);
                bookmark->setBand(settings->value(s).toLongLong());
                s = QString("frequency.%1").arg(i);
                bookmark->setFrequency(settings->value(s).toLongLong());
                s = QString("mode.%1").arg(i);
                bookmark->setMode(settings->value(s).toInt());
                s = QString("filter.%1").arg(i);
                bookmark->setFilter(settings->value(s).toInt());
                bookmarks.append(bookmark);
                qDebug()<<"  append "<<bookmark->getTitle();
            } else {
                qDebug()<<"  no entry for"<<s;
            }

        }
    } else {
        qDebug()<<"  no entries";
    }
    settings->endGroup();
}

void Bookmarks::add(Bookmark* bookmark) {
    qDebug()<<"Bookmarks::add";
    bookmarks.append(bookmark);
}

void Bookmarks::remove(int index) {
    qDebug()<<"Bookmarks::remove";
    bookmarks.remove(index);
}

Bookmark* Bookmarks::at(int index) {
    qDebug()<<"Bookmarks::at";
    return bookmarks.at(index);
}

int Bookmarks::count() {
    qDebug()<<"Bookmarks::count";
    return bookmarks.count();
}

void Bookmarks::select(QAction* action)
{
    qDebug()<<"Bookmarks::select";

    // Always start from no selection: with no saved bookmarks the old loop
    // never ran, so the previous selection stayed and a preset chosen next
    // tuned to the one before it.
    currentBookmark = NULL;
    for (int i = 0; i < bookmarks.size(); i++)
    {
        if (action->text() == bookmarks.at(i)->getTitle())
        {
            currentBookmark = bookmarks.at(i);
            return;
        }
    }
    for (int i = 0; i < cbChannels.size(); i++)
    {
        if (action->text() == cbChannels.at(i)->getTitle())
        {
            currentBookmark = cbChannels.at(i);
            return;
        }
    }
}

bool Bookmarks::hasSelection(void)
{
    return currentBookmark != NULL;
}

int Bookmarks::getBand() {
    return currentBookmark->getBand();
}

long long Bookmarks::getFrequency() {
    return currentBookmark->getFrequency();
}

int Bookmarks::getMode() {
    return currentBookmark->getMode();
}

int Bookmarks::getFilter() {
    return currentBookmark->getFilter();
}
