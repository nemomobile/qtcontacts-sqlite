/*
 * Copyright (C) 2013 Jolla Ltd. <matthew.vogt@jollamobile.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#ifdef USING_QTPIM
#include "conversion_p.h"

#include <QContactAnniversary>
#include <QContactGender>
#include <QContactOnlineAccount>
#include <QContactPhoneNumber>
#include <QContactUrl>

USE_CONTACTS_NAMESPACE

namespace Conversion {

int propertyValue(const QString &name, const QMap<QString, int> &propertyValues)
{
    QMap<QString, int>::const_iterator it = propertyValues.find(name);
    if (it != propertyValues.end()) {
        return *it;
    }
    return -1;
}

QList<int> propertyValueList(const QStringList &names, const QMap<QString, int> &propertyValues)
{
    QList<int> rv;
    foreach (const QString &name, names) {
        rv.append(propertyValue(name, propertyValues));
    }
    return rv;
}

QString propertyName(int value, const QMap<int, QString> &propertyNames)
{
    QMap<int, QString>::const_iterator it = propertyNames.find(value);
    if (it != propertyNames.end()) {
        return *it;
    }
    return QString();
}

QStringList propertyNameList(const QList<int> &values, const QMap<int, QString> &propertyNames)
{
    QStringList list;
    foreach (int value, values) {
        list.append(propertyName(value, propertyNames));
    }
    return list;
}

namespace OnlineAccount {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Sip"), QContactOnlineAccount::SubTypeSip);
    rv.insert(QString::fromLatin1("SipVoip"), QContactOnlineAccount::SubTypeSipVoip);
    rv.insert(QString::fromLatin1("Impp"), QContactOnlineAccount::SubTypeImpp);
    rv.insert(QString::fromLatin1("VideoShare"), QContactOnlineAccount::SubTypeVideoShare);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactOnlineAccount::SubTypeSip, "Sip");
    rv.insert(QContactOnlineAccount::SubTypeSipVoip, "SipVoip");
    rv.insert(QContactOnlineAccount::SubTypeImpp, "Impp");
    rv.insert(QContactOnlineAccount::SubTypeVideoShare, "VideoShare");

    return rv;
}

static QMap<QString, int> protocolValues()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Unknown"), QContactOnlineAccount::ProtocolUnknown);
    rv.insert(QString::fromLatin1("Aim"), QContactOnlineAccount::ProtocolAim);
    rv.insert(QString::fromLatin1("Icq"), QContactOnlineAccount::ProtocolIcq);
    rv.insert(QString::fromLatin1("Irc"), QContactOnlineAccount::ProtocolIrc);
    rv.insert(QString::fromLatin1("Jabber"), QContactOnlineAccount::ProtocolJabber);
    rv.insert(QString::fromLatin1("Msn"), QContactOnlineAccount::ProtocolMsn);
    rv.insert(QString::fromLatin1("Qq"), QContactOnlineAccount::ProtocolQq);
    rv.insert(QString::fromLatin1("Skype"), QContactOnlineAccount::ProtocolSkype);
    rv.insert(QString::fromLatin1("Yahoo"), QContactOnlineAccount::ProtocolYahoo);

    return rv;
}

static QMap<int, QString> protocolNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactOnlineAccount::ProtocolUnknown, "Unknown");
    rv.insert(QContactOnlineAccount::ProtocolAim, "Aim");
    rv.insert(QContactOnlineAccount::ProtocolIcq, "Icq");
    rv.insert(QContactOnlineAccount::ProtocolIrc, "Irc");
    rv.insert(QContactOnlineAccount::ProtocolJabber, "Jabber");
    rv.insert(QContactOnlineAccount::ProtocolMsn, "Msn");
    rv.insert(QContactOnlineAccount::ProtocolQq, "Qq");
    rv.insert(QContactOnlineAccount::ProtocolSkype, "Skype");
    rv.insert(QContactOnlineAccount::ProtocolYahoo, "Yahoo");

    return rv;
}

QList<int> subTypeList(const QStringList &names)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValueList(names, subTypes);
}

QStringList subTypeList(const QList<int> &values)
{
    static const QMap<int, QString> typeNames(subTypeNames());

    return propertyNameList(values, typeNames);
}

int protocol(const QString &name)
{
    static const QMap<QString, int> protocols(protocolValues());

    return propertyValue(name, protocols);
}

QString protocol(int type)
{
    static const QMap<int, QString> names(protocolNames());

    return propertyName(type, names);
}

}

namespace PhoneNumber {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Landline"), QContactPhoneNumber::SubTypeLandline);
    rv.insert(QString::fromLatin1("Mobile"), QContactPhoneNumber::SubTypeMobile);
    rv.insert(QString::fromLatin1("Fax"), QContactPhoneNumber::SubTypeFax);
    rv.insert(QString::fromLatin1("Pager"), QContactPhoneNumber::SubTypePager);
    rv.insert(QString::fromLatin1("Voice"), QContactPhoneNumber::SubTypeVoice);
    rv.insert(QString::fromLatin1("Modem"), QContactPhoneNumber::SubTypeModem);
    rv.insert(QString::fromLatin1("Video"), QContactPhoneNumber::SubTypeVideo);
    rv.insert(QString::fromLatin1("Car"), QContactPhoneNumber::SubTypeCar);
    rv.insert(QString::fromLatin1("BulletinBoardSystem"), QContactPhoneNumber::SubTypeBulletinBoardSystem);
    rv.insert(QString::fromLatin1("MessagingCapable"), QContactPhoneNumber::SubTypeMessagingCapable);
    rv.insert(QString::fromLatin1("Assistant"), QContactPhoneNumber::SubTypeAssistant);
    rv.insert(QString::fromLatin1("DtmfMenu"), QContactPhoneNumber::SubTypeDtmfMenu);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactPhoneNumber::SubTypeLandline, "Landline");
    rv.insert(QContactPhoneNumber::SubTypeMobile, "Mobile");
    rv.insert(QContactPhoneNumber::SubTypeFax, "Fax");
    rv.insert(QContactPhoneNumber::SubTypePager, "Pager");
    rv.insert(QContactPhoneNumber::SubTypeVoice, "Voice");
    rv.insert(QContactPhoneNumber::SubTypeModem, "Modem");
    rv.insert(QContactPhoneNumber::SubTypeVideo, "Video");
    rv.insert(QContactPhoneNumber::SubTypeCar, "Car");
    rv.insert(QContactPhoneNumber::SubTypeBulletinBoardSystem, "BulletinBoardSystem");
    rv.insert(QContactPhoneNumber::SubTypeMessagingCapable, "MessagingCapable");
    rv.insert(QContactPhoneNumber::SubTypeAssistant, "Assistant");
    rv.insert(QContactPhoneNumber::SubTypeDtmfMenu, "DtmfMenu");

    return rv;
}

QList<int> subTypeList(const QStringList &names)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValueList(names, subTypes);
}

QStringList subTypeList(const QList<int> &values)
{
    static const QMap<int, QString> typeNames(subTypeNames());

    return propertyNameList(values, typeNames);
}

}

namespace Anniversary {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Wedding"), QContactAnniversary::SubTypeWedding);
    rv.insert(QString::fromLatin1("Engagement"), QContactAnniversary::SubTypeEngagement);
    rv.insert(QString::fromLatin1("House"), QContactAnniversary::SubTypeHouse);
    rv.insert(QString::fromLatin1("Employment"), QContactAnniversary::SubTypeEmployment);
    rv.insert(QString::fromLatin1("Memorial"), QContactAnniversary::SubTypeMemorial);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactAnniversary::SubTypeWedding, "Wedding");
    rv.insert(QContactAnniversary::SubTypeEngagement, "Engagement");
    rv.insert(QContactAnniversary::SubTypeHouse, "House");
    rv.insert(QContactAnniversary::SubTypeEmployment, "Employment");
    rv.insert(QContactAnniversary::SubTypeMemorial, "Memorial");

    return rv;
}

int subType(const QString &name)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValue(name, subTypes);
}

QString subType(int type)
{
    static const QMap<int, QString> subTypes(subTypeNames());

    return propertyName(type, subTypes);
}

}

namespace Url {

static QMap<QString, int> subTypeValues()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("HomePage"), QContactUrl::SubTypeHomePage);
    rv.insert(QString::fromLatin1("Blog"), QContactUrl::SubTypeBlog);
    rv.insert(QString::fromLatin1("Favourite"), QContactUrl::SubTypeFavourite);

    return rv;
}

static QMap<int, QString> subTypeNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactUrl::SubTypeHomePage, "HomePage");
    rv.insert(QContactUrl::SubTypeBlog, "Blog");
    rv.insert(QContactUrl::SubTypeFavourite, "Favourite");

    return rv;
}

int subType(const QString &name)
{
    static const QMap<QString, int> subTypes(subTypeValues());

    return propertyValue(name, subTypes);
}

QString subType(int type)
{
    static const QMap<int, QString> subTypes(subTypeNames());

    return propertyName(type, subTypes);
}

}

namespace Gender {

static QMap<QString, int> genderValues()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Male"), QContactGender::GenderMale);
    rv.insert(QString::fromLatin1("Female"), QContactGender::GenderFemale);
    rv.insert(QString::fromLatin1(""), QContactGender::GenderUnspecified);

    return rv;
}

static QMap<int, QString> genderNames()
{
    QMap<int, QString> rv;

    rv.insert(QContactGender::GenderMale, QString::fromLatin1("Male"));
    rv.insert(QContactGender::GenderFemale, QString::fromLatin1("Female"));
    rv.insert(QContactGender::GenderUnspecified, QString::fromLatin1(""));

    return rv;
}

int gender(const QString &name)
{
    static const QMap<QString, int> genders(genderValues());

    return propertyValue(name, genders);
}

QString gender(int type)
{
    static const QMap<int, QString> genders(genderNames());

    return propertyName(type, genders);
}

}

}

#endif

