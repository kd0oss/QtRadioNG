/* Copyright (C)
* 2026 - Rick Schnicker KD0OSS
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

/*
 * A protocol plugin's panel, built from the <panel> of its description:
 *   text, spin, combo, check, button   settings, sent to the plugin as
 *                                      key=value (persist="1": saved here
 *                                      and sent again on every connect)
 *   status, meter, log                 shown from the plugin's status lines
 */

#ifndef PLUGINFRAME_H
#define PLUGINFRAME_H

#include <QGroupBox>
#include <QMap>
#include <QString>
#include <QSettings>

class QFormLayout;
class QLabel;
class QProgressBar;
class QPlainTextEdit;

class PluginFrame : public QGroupBox {
    Q_OBJECT
public:
    PluginFrame(int pluginId, const QString &name, const QString &title, const QString &xml, QWidget *parent = nullptr);
    virtual ~PluginFrame();

    int  pluginId() const { return id; }
    QString pluginName() const { return name; }
    void sendAll();                          // every current setting value
    void setStatus(const QString &text);     // "stream=n" then key=value lines

signals:
    void setting(int pluginId, QString key, QString value);

private:
    struct Meter
    {
        QProgressBar *bar;
        QString unit;
    };

    void changed(const QString &key, const QString &value);
    void build(const QString &xml);

    int            id;
    QString        name;
    QSettings     *settings;
    QFormLayout   *form;
    QMap<QString, QString>        values;     // current settings
    QMap<QString, bool>           persist;
    QMap<QString, QLabel*>        statusLabels;
    QMap<QString, Meter>          meters;
    QMap<QString, QPlainTextEdit*> logs;
};

#endif // PLUGINFRAME_H
