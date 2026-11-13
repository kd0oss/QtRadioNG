#include "XvtrEntry.h"

XvtrEntry::XvtrEntry(QString t,long long minF,long long maxF,long long ifF,long long freq,int m,int filt) {
    title=t;
    minFrequency=minF;
    maxFrequency=maxF;
    ifFrequency=ifF;
    frequency=freq;
    mode=m;
    filter=filt;
}

QString XvtrEntry::getTitle() {
    return title;
}

long long XvtrEntry::getMinFrequency() {
    return minFrequency;
}

long long XvtrEntry::getMaxFrequency() {
    return maxFrequency;
}

long long XvtrEntry::getIFFrequency() {
    return ifFrequency;
}

