#pragma once
#include "mining/source_decimal.h"
#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
// ONE declared-identity ABI shared by observation and explicit legacy audit.
// This is not current ownership, process authentication or source completeness.
inline QJsonObject nativeSourceObservationIdentity(const QJsonObject& manifest,const QString& run,const QString& device){
    const auto fail=[](const char* message){throw std::runtime_error(message);};
    static const QRegularExpression runPattern("^[0-9a-f]{32}$");
    if(!runPattern.match(run).hasMatch()||device.isEmpty()||manifest.value("schema")!="pgn-native-process-1"||
       manifest.value("run")!=run||manifest.value("uuid").toString().compare(device,Qt::CaseInsensitive)!=0)
        fail("Native observation source identity mismatch");
    const auto start=manifest.value("start").toString(),end=manifest.value("end").toString();
    if(start.size()>160||end.size()>160||!source_decimal::canonical(start)||!source_decimal::canonical(end)||!source_decimal::less(start,end))
        fail("Native observation exact source range invalid");
    const auto pid=manifest.value("pid");
    if(pid.toVariant().metaType().id()!=QMetaType::LongLong||pid.toInteger()<=0||pid.toInteger()>std::numeric_limits<quint32>::max())
        fail("Native observation declared PID invalid");
    const auto birth=manifest.value("birth").toString();
    if(birth.size()>64||!birth.endsWith('Z')||!QDateTime::fromString(birth,Qt::ISODateWithMs).isValid())
        fail("Native observation declared process birth invalid");
    return {{"run",run},{"uuid",device.toLower()},{"start",start},{"end",end},{"pid",pid},{"birth",birth}};
}
}
