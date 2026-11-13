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

#include <QXmlStreamReader>
#include <QDebug>

#include "pluginregistry.h"
#include "../plugins/include/plugin_protocol.h"

PluginFilters::PluginFilters()
{
    setPassband(-5000, 5000);
    selectFilter(2);
}


/* Entries around the plugin's own bandwidth; the plugin decodes its channel
 * whatever is chosen, the filter only shapes the panadapter and meters. */
void PluginFilters::setPassband(int low, int high)
{
    static const double scale[MAX_FILTERS - 1] = {2.0, 1.5, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3};
    double centre = (low + high) / 2.0;
    double half = (high - low) / 2.0;

    for (int k = 0; k < MAX_FILTERS - 1; k++)
    {
        int l = (int)(centre - half * scale[k]);
        int h = (int)(centre + half * scale[k]);
        double bw = (h - l) / 1000.0;
        filters[k].init(QString("%1k").arg(bw, 0, 'g', 3), l, h);
    }
    filters[MAX_FILTERS - 1].init("Vari", low, high);
}


PluginRegistry *PluginRegistry::instance()
{
    static PluginRegistry *r = new PluginRegistry();
    return r;
}


bool PluginRegistry::isPluginMode(int mode)
{
    return mode >= PLUGIN_FIRST_MODE && mode < PLUGIN_FIRST_MODE + PLUGIN_MAX * PLUGIN_MAX_MODES;
}


static int attr(const QXmlStreamAttributes &a, const char *name, int def)
{
    bool ok = false;
    int v = a.value(QLatin1String(name)).toInt(&ok);
    return ok ? v : def;
}


/* <plugins><entry id="0" first_mode="32"><plugin name=..>..</plugin></entry>..</plugins> */
void PluginRegistry::setList(const QString &xml)
{
    QList<PluginInfo> parsed;
    QXmlStreamReader r(xml);
    PluginInfo *cur = nullptr;

    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement())
        {
            const QStringView n = r.name();
            if (n == QLatin1String("entry"))
            {
                parsed.append(PluginInfo());
                cur = &parsed.last();
                cur->id = attr(r.attributes(), "id", -1);
                cur->firstMode = attr(r.attributes(), "first_mode", PLUGIN_FIRST_MODE);
            }
            else if (n == QLatin1String("plugin") && cur != nullptr)
            {
                cur->name = r.attributes().value(QLatin1String("name")).toString();
                cur->title = r.attributes().value(QLatin1String("title")).toString();
                if (cur->title.isEmpty())
                    cur->title = cur->name;
            }
            else if (n == QLatin1String("mode") && cur != nullptr && cur->modes.size() < PLUGIN_MAX_MODES)
            {
                PluginMode m;
                m.name = r.attributes().value(QLatin1String("name")).toString();
                m.number = cur->firstMode + cur->modes.size();
                m.low = attr(r.attributes(), "filter_low", -5000);
                m.high = attr(r.attributes(), "filter_high", 5000);
                if (!m.name.isEmpty())
                    cur->modes.append(m);
            }
        }
        else if (r.isEndElement() && r.name() == QLatin1String("entry"))
            cur = nullptr;
    }
    if (r.hasError())
        qDebug() << "PluginRegistry: bad plugin list:" << r.errorString();

    // keep each plugin's own XML for its panel
    int pos = 0;
    for (PluginInfo &p : parsed)
    {
        int a = xml.indexOf("<plugin", pos);
        int b = xml.indexOf("</plugin>", a);
        if (a >= 0 && b > a)
        {
            p.xml = xml.mid(a, b - a + 9);
            pos = b;
        }
        for (const PluginMode &m : p.modes)
        {
            filters(m.number);
            modeFilters[m.number]->setPassband(m.low, m.high);
            knownNames[m.number] = m.name;
        }
    }
    list = parsed;
    emit changed();
}


void PluginRegistry::clear()
{
    list.clear();
    emit changed();
}


const PluginMode *PluginRegistry::mode(int number) const
{
    for (const PluginInfo &p : list)
        for (const PluginMode &m : p.modes)
            if (m.number == number)
                return &m;
    return nullptr;
}


QString PluginRegistry::modeName(int number) const
{
    const PluginMode *m = mode(number);
    if (m != nullptr)
        return m->name;
    return knownNames.value(number, QString());
}


FiltersBase *PluginRegistry::filters(int number)
{
    if (!modeFilters.contains(number))
        modeFilters[number] = new PluginFilters();
    return modeFilters[number];
}
