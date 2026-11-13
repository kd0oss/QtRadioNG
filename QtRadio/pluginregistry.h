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
 * The protocol plugins dspserver reports (QUESTION QPLUGINS): their modes,
 * mode numbers and filters, and their panel descriptions.  One instance for
 * the program, so Mode and Radio can name and filter plugin modes too.
 */

#ifndef PLUGINREGISTRY_H
#define PLUGINREGISTRY_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

#include "Filters.h"

class PluginFilters : public FiltersBase {
public:
    PluginFilters();
    void setPassband(int low, int high);    // the plugin's filter is the 3rd entry
};

struct PluginMode
{
    QString name;
    int     number;
    int     low;
    int     high;
};

struct PluginInfo
{
    int     id;
    QString name;
    QString title;
    int     firstMode;
    QList<PluginMode> modes;
    QString xml;              // the plugin's own description (panel ...)
};

class PluginRegistry : public QObject {
    Q_OBJECT
public:
    static PluginRegistry *instance();
    static bool isPluginMode(int mode);

    void setList(const QString &xml);
    void clear();
    const QList<PluginInfo> &plugins() const { return list; }
    const PluginMode *mode(int number) const;
    QString modeName(int number) const;      // "" if unknown
    FiltersBase *filters(int number);         // stays valid for the program's life

signals:
    void changed();

private:
    PluginRegistry() {}
    QList<PluginInfo> list;
    QMap<int, PluginFilters*> modeFilters;
    QMap<int, QString> knownNames;            // names of modes seen, for saved settings
};

#endif // PLUGINREGISTRY_H
