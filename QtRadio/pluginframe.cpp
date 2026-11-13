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

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QXmlStreamReader>
#include <QFontMetrics>
#include <QDebug>

#include "pluginframe.h"

PluginFrame::PluginFrame(int pluginId, const QString &pname, const QString &title, const QString &xml, QWidget *parent)
    : QGroupBox(title, parent), id(pluginId), name(pname)
{
    settings = new QSettings("FreeSDR", "QtRadioII");
    form = new QFormLayout(this);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    build(xml);
}


PluginFrame::~PluginFrame()
{
    delete settings;
}


void PluginFrame::changed(const QString &key, const QString &value)
{
    values[key] = value;
    if (persist.value(key, false))
    {
        settings->beginGroup("Plugin_" + name);
        settings->setValue(key, value);
        settings->endGroup();
    }
    emit setting(id, key, value);
}


void PluginFrame::sendAll()
{
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        emit setting(id, it.key(), it.value());
}


void PluginFrame::build(const QString &xml)
{
    QXmlStreamReader r(xml);
    bool inPanel = false;

    settings->beginGroup("Plugin_" + name);
    while (!r.atEnd())
    {
        r.readNext();
        if (r.isEndElement() && r.name() == QLatin1String("panel"))
            inPanel = false;
        if (!r.isStartElement())
            continue;
        if (r.name() == QLatin1String("panel"))
        {
            inPanel = true;
            continue;
        }
        if (!inPanel)
            continue;

        const QXmlStreamAttributes a = r.attributes();
        const QString kind = r.name().toString();
        const QString key = a.value(QLatin1String("key")).toString();
        const QString label = a.value(QLatin1String("label")).toString();
        const QString def = a.value(QLatin1String("default")).toString();
        const bool keep = a.value(QLatin1String("persist")) == QLatin1String("1");
        const QString value = keep ? settings->value(key, def).toString() : def;
        if (key.isEmpty())
            continue;

        if (kind == QLatin1String("text"))
        {
            QLineEdit *w = new QLineEdit(value, this);
            int max = a.value(QLatin1String("max")).toInt();
            if (max > 0)
                w->setMaxLength(max);
            connect(w, &QLineEdit::editingFinished, this, [this, w, key]() {
                if (values.value(key) != w->text())
                    changed(key, w->text());
            });
            form->addRow(label, w);
        }
        else if (kind == QLatin1String("spin"))
        {
            QSpinBox *w = new QSpinBox(this);
            w->setRange(a.value(QLatin1String("min")).toInt(), a.value(QLatin1String("max")).toInt());
            w->setValue(value.toInt());
            connect(w, qOverload<int>(&QSpinBox::valueChanged), this, [this, key](int v) {
                changed(key, QString::number(v));
            });
            form->addRow(label, w);
        }
        else if (kind == QLatin1String("combo"))
        {
            QComboBox *w = new QComboBox(this);
            w->addItems(a.value(QLatin1String("items")).toString().split(',', Qt::SkipEmptyParts));
            int i = w->findText(value);
            w->setCurrentIndex(i >= 0 ? i : 0);
            connect(w, &QComboBox::currentTextChanged, this, [this, key](const QString &t) {
                changed(key, t);
            });
            form->addRow(label, w);
            values[key] = w->currentText();
            persist[key] = keep;
            continue;
        }
        else if (kind == QLatin1String("check"))
        {
            QCheckBox *w = new QCheckBox(label, this);
            w->setChecked(value.toInt() != 0);
            connect(w, &QCheckBox::toggled, this, [this, key](bool on) {
                changed(key, on ? "1" : "0");
            });
            form->addRow(QString(), w);
            values[key] = w->isChecked() ? "1" : "0";
            persist[key] = keep;
            continue;
        }
        else if (kind == QLatin1String("button"))
        {
            QPushButton *w = new QPushButton(label, this);
            connect(w, &QPushButton::clicked, this, [this, key]() {
                emit setting(id, key, "1");          // an action, not a value
            });
            form->addRow(QString(), w);
            continue;
        }
        else if (kind == QLatin1String("status"))
        {
            QLabel *w = new QLabel(QString(), this);
            w->setTextInteractionFlags(Qt::TextSelectableByMouse);
            statusLabels[key] = w;
            form->addRow(label, w);
            continue;
        }
        else if (kind == QLatin1String("meter"))
        {
            Meter m;
            m.bar = new QProgressBar(this);
            m.bar->setRange(a.value(QLatin1String("min")).toInt(), a.value(QLatin1String("max")).toInt());
            m.bar->setValue(m.bar->minimum());
            m.bar->setFormat(QString());
            m.unit = a.value(QLatin1String("unit")).toString();
            meters[key] = m;
            form->addRow(label, m.bar);
            continue;
        }
        else if (kind == QLatin1String("log"))
        {
            QPlainTextEdit *w = new QPlainTextEdit(this);
            int lines = a.value(QLatin1String("lines")).toInt();
            w->setReadOnly(true);
            w->setMaximumBlockCount(500);
            w->setFixedHeight(QFontMetrics(w->font()).lineSpacing() * (lines > 0 ? lines : 4) + 10);
            logs[key] = w;
            form->addRow(label, w);
            continue;
        }
        else
        {
            qDebug() << "PluginFrame:" << name << "unknown panel element" << kind;
            continue;
        }
        values[key] = value;
        persist[key] = keep;
    }
    settings->endGroup();
    if (r.hasError())
        qDebug() << "PluginFrame:" << name << "bad panel description:" << r.errorString();
}


void PluginFrame::setStatus(const QString &text)
{
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines)
    {
        int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = line.left(eq);
        const QString value = line.mid(eq + 1);

        if (statusLabels.contains(key))
            statusLabels[key]->setText(value);
        else if (meters.contains(key))
        {
            Meter &m = meters[key];
            double v = value.toDouble();
            m.bar->setValue(qBound(m.bar->minimum(), (int)qRound(v), m.bar->maximum()));
            m.bar->setFormat(value + (m.unit.isEmpty() ? QString() : " " + m.unit));
        }
        else if (logs.contains(key))
            logs[key]->appendPlainText(value);
        else if (key.endsWith(".clear") && logs.contains(key.chopped(6)))
            logs[key.chopped(6)]->clear();
    }
}
