/* 
 * File:   LSBFilter.h
 * Author: John Melton, G0ORX/N6LYT
 *
 * Created on 14 August 2010, 10:15
 */

/* Copyright (C)
* 2009 - John Melton, G0ORX/N6LYT
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

#ifndef FILTERS_H
#define	FILTERS_H

#include <QObject>
#include <QDebug>
#include <Filters.h>

#define MAX_FILTERS 11

class Filter : QObject {
    Q_OBJECT
public:
    Filter();
    Filter(QString t,int l,int h);
    virtual ~Filter();
    void init(QString t,int l,int h);
    void setText(QString t);
    void setLow(int l);
    void setHigh(int h);
    QString getText();
    int getLow();
    int getHigh();
private:
    QString text;
    int low;
    int high;
};

class FiltersBase : public QObject {
    Q_OBJECT
public:
    FiltersBase();
    FiltersBase(const FiltersBase& orig);
    virtual ~FiltersBase();
    QString getText(int f);
    int getSelected();
    void selectFilter(int f);
    QString getText();
    int getLow();
    int getHigh();


    Filter filters[MAX_FILTERS];
private:
    int currentFilter;

};


class Filters : public QObject {
    Q_OBJECT
public:
    Filters();
    Filters(const Filters& orig);
    virtual ~Filters();
    void selectFilters(FiltersBase* f);
    void selectFilter(int f);
    int getFilter();
    int getLow();
    int getHigh();
    void setIndex(int8_t index);
    int8_t index;
    QString getText();
    FiltersBase *getCurrentFilters(void);

signals:
    void filtersChanged(int8_t, FiltersBase* previousFilters, FiltersBase* newFilters);
    void filterChanged(int8_t, int previousFitler,int newFilter);

private:
    FiltersBase* currentFilters;
    
};


class AMFilters : public FiltersBase {
public:
    AMFilters();
    virtual ~AMFilters();
private:
};

class CWLFilters : public FiltersBase {
public:
    CWLFilters();
    virtual ~CWLFilters();
private:
};

class CWUFilters : public FiltersBase {
public:
    CWUFilters();
    virtual ~CWUFilters();
private:

};

class DIGLFilters : public FiltersBase {
public:
    DIGLFilters();
    virtual ~DIGLFilters();
private:

};

class DIGUFilters : public FiltersBase {
public:
    DIGUFilters();
    virtual ~DIGUFilters();
private:

};

class DSBFilters : public FiltersBase {
public:
    DSBFilters();
    virtual ~DSBFilters();
private:

};

class FMNFilters : public FiltersBase {
public:
    FMNFilters();
    virtual ~FMNFilters();
private:

};

class LSBFilters : public FiltersBase {
public:
    LSBFilters();
    virtual ~LSBFilters();
private:

};

class USBFilters : public FiltersBase {
public:
    USBFilters();
    virtual ~USBFilters();
private:

};

class SAMFilters : public FiltersBase {
public:
    SAMFilters();
    virtual ~SAMFilters();
private:

};


#endif	/* FILTERS_H */

