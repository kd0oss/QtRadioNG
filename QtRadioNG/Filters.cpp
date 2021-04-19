/* 
 * File:   Filters.cpp
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


#include "Filters.h"
#include "Mode.h"

FiltersBase::FiltersBase()
{
}

FiltersBase::FiltersBase(const FiltersBase& orig)
{
}

FiltersBase::~FiltersBase()
{
}

int FiltersBase::getSelected()
{
    return currentFilter;
}

void FiltersBase::selectFilter(int f)
{
    currentFilter=f;
}

QString FiltersBase::getText(int f)
{
    return filters[f].getText();
}

QString FiltersBase::getText()
{
    return filters[currentFilter].getText();
}

int FiltersBase::getLow()
{
    return filters[currentFilter].getLow();
}

int FiltersBase::getHigh()
{
    return filters[currentFilter].getHigh();
}


/***********************************************************/
Filter::Filter()
{
}

Filter::Filter(QString t,int l,int h)
{
    text=t;
    low=l;
    high=h;
}

Filter::~Filter()
{
}

void Filter::init(QString t,int l,int h)
{
    text=t;
    low=l;
    high=h;
}

void Filter::setText(QString t)
{
    text=t;
}

void Filter::setLow(int l)
{
    low=l;
}

void Filter::setHigh(int h)
{
    high=h;
}

QString Filter::getText()
{
    return text;
}

int Filter::getLow()
{
    return low;
}

int Filter::getHigh()
{
    return high;
}


/**********************************************************/
Filters::Filters()
{
    currentFilters=NULL;
}

Filters::Filters(const Filters& orig)
{
}

Filters::~Filters()
{
}

void Filters::selectFilters(FiltersBase* filters)
{
    FiltersBase* oldFilters=currentFilters;
    currentFilters=filters;
    qDebug()<<Q_FUNC_INFO<<":   Connecting to UI::filtersChanged. currentFilters = "<<currentFilters->getSelected();
    emit filtersChanged(oldFilters,currentFilters);
}

void Filters::selectFilter(int f)
{
    int previousFilter=currentFilters->getSelected();
    currentFilters->selectFilter(f);
    emit filterChanged(previousFilter,f);
}

int Filters:: getFilter()
{
    return currentFilters->getSelected();
}

int Filters::getLow()
{
    return currentFilters->getLow();
}

int Filters::getHigh()
{
    return currentFilters->getHigh();
}

QString Filters::getText()
{
    return currentFilters->getText();
}

/********************************************************/
FMNFilters::FMNFilters()
{
    filters[0].init("80k", -40000,40000);
    filters[1].init("12k",  -6000, 6000);
    filters[2].init("10k",  -5000, 5000);
    filters[3].init("8k",   -4000, 4000);
    filters[4].init("6.6k", -3300, 3300);
    filters[5].init("5.2k", -2600, 2600);
    filters[6].init("4.0k", -2000, 2000);
    filters[7].init("3.1k", -1550, 1550);
    filters[8].init("2.9k", -1450, 1450);
    filters[9].init("2.4k", -1200, 1200);
    filters[10].init("Vari",-2600, 2600);

    selectFilter(4);
}

FMNFilters::~FMNFilters()
{
}

AMFilters::AMFilters()
{
    filters[0].init("16k",  -8000,8000);
    filters[1].init("12k",  -6000,6000);
    filters[2].init("10k",  -5000,5000);
    filters[3].init("8k",   -4000,4000);
    filters[4].init("6.6k", -3300,3300);
    filters[5].init("5.2k", -2600,2600);
    filters[6].init("4.0k", -2000,2000);
    filters[7].init("3.1k", -1550,1550);
    filters[8].init("2.9k", -1450,1450);
    filters[9].init("2.4k", -1200,1200);
    filters[10].init("Vari",-2600,2600);

    selectFilter(3);
}

AMFilters::~AMFilters()
{
}

CWLFilters::CWLFilters()
{
    filters[0].init("1.0k", 500,500);
    filters[1].init("800",  400,400);
    filters[2].init("750",  375,375);
    filters[3].init("600",  300,300);
    filters[4].init("500",  250,250);
    filters[5].init("400",  200,200);
    filters[6].init("250",  125,125);
    filters[7].init("100",   50, 50);
    filters[8].init("50",    25, 25);
    filters[9].init("25",    13, 13);
    filters[10].init("Vari",200,200);

    selectFilter(5);
}

CWLFilters::~CWLFilters()
{
}

CWUFilters::CWUFilters()
{
    filters[0].init("1.0k", 500,500);
    filters[1].init("800",  400,400);
    filters[2].init("750",  375,375);
    filters[3].init("600",  300,300);
    filters[4].init("500",  250,250);
    filters[5].init("400",  200,200);
    filters[6].init("250",  125,125);
    filters[7].init("100",   50, 50);
    filters[8].init("50",    25, 25);
    filters[9].init("25",    13, 13);
    filters[10].init("Vari",200,200);

    selectFilter(5);
}

CWUFilters::~CWUFilters()
{
}

DIGLFilters::DIGLFilters()
{
    filters[0].init("5.0k", -5150,-150);
    filters[1].init("4.4k", -4550,-150);
    filters[2].init("3.8k", -3950,-150);
    filters[3].init("3.3k", -3450,-150);
    filters[4].init("2.9k", -3050,-150);
    filters[5].init("2.7k", -2850,-150);
    filters[6].init("2.4k", -2550,-150);
    filters[7].init("2.1k", -2250,-150);
    filters[8].init("1.8k", -1950,-150);
    filters[9].init("1.0k", -1150,-150);
    filters[10].init("Vari",-2550,-150);

    selectFilter(3);
}

DIGLFilters::~DIGLFilters()
{
}

DIGUFilters::DIGUFilters()
{
    filters[0].init("5.0k", 150,5150);
    filters[1].init("4.4k", 150,4550);
    filters[2].init("3.8k", 150,3950);
    filters[3].init("3.3k", 150,3450);
    filters[4].init("2.9k", 150,3050);
    filters[5].init("2.7k", 150,2850);
    filters[6].init("2.4k", 150,2550);
    filters[7].init("2.1k", 150,2250);
    filters[8].init("1.8k", 150,1950);
    filters[9].init("1.0k", 150,1150);
    filters[10].init("Vari",150,2550);

    selectFilter(3);
}

DIGUFilters::~DIGUFilters()
{
}

DSBFilters::DSBFilters()
{
    filters[0].init("16k",  -8000,8000);
    filters[1].init("12k",  -6000,6000);
    filters[2].init("10k",  -5000,5000);
    filters[3].init("8k",   -4000,4000);
    filters[4].init("6.6k", -3300,3300);
    filters[5].init("5.2k", -2600,2600);
    filters[6].init("4.0k", -2000,2000);
    filters[7].init("3.1k", -1550,1550);
    filters[8].init("2.9k", -1450,1450);
    filters[9].init("2.4k", -1200,1200);
    filters[10].init("Vari",-2600,2600);

    selectFilter(4);
}

DSBFilters::~DSBFilters()
{
}

LSBFilters::LSBFilters()
{

    filters[0].init("5.0k", -5150,-150);
    filters[1].init("4.4k", -4550,-150);
    filters[2].init("3.8k", -3950,-150);
    filters[3].init("3.3k", -3450,-150);
    filters[4].init("2.9k", -3050,-150);
    filters[5].init("2.7k", -2850,-150);
    filters[6].init("2.4k", -2550,-150);
    filters[7].init("2.1k", -2250,-150);
    filters[8].init("1.8k", -1950,-150);
    filters[9].init("1.0k", -1150,-150);
    filters[10].init("Vari",-2550,-150);

    selectFilter(3);
}

LSBFilters::~LSBFilters()
{
}

SAMFilters::SAMFilters()
{
    filters[0].init("16k",-  8000,8000);
    filters[1].init("12k",  -6000,6000);
    filters[2].init("10k",  -5000,5000);
    filters[3].init("8k",   -4000,4000);
    filters[4].init("6.6k", -3300,3300);
    filters[5].init("5.2k", -2600,2600);
    filters[6].init("4.0k", -2000,2000);
    filters[7].init("3.1k", -1550,1550);
    filters[8].init("2.9k", -1450,1450);
    filters[9].init("2.4k", -1200,1200);
    filters[10].init("Vari",-2600,2600);

    selectFilter(3);
}

SAMFilters::~SAMFilters()
{
}

USBFilters::USBFilters()
{
    qDebug() << "USBFilters";

    filters[0].init("5.0k", 150,5150);
    filters[1].init("4.4k", 150,4550);
    filters[2].init("3.8k", 150,3950);
    filters[3].init("3.3k", 150,3450);
    filters[4].init("2.9k", 150,3050);
    filters[5].init("2.7k", 150,2850);
    filters[6].init("2.4k", 150,2550);
    filters[7].init("2.1k", 150,2250);
    filters[8].init("1.8k", 150,1950);
    filters[9].init("1.0k", 150,1150);
    filters[10].init("Vari",150,2550);

    selectFilter(3);
}

USBFilters::~USBFilters()
{
}
