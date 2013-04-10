/*
 * Copyright (C) 2013 Jolla Ltd. <andrew.den.exter@jollamobile.com>
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

#include "contactreader.h"


#include <QContactAddress>
#include <QContactAnniversary>
#include <QContactAvatar>
#include <QContactBirthday>
#include <QContactEmailAddress>
#include <QContactFavorite>
#include <QContactGender>
#include <QContactGlobalPresence>
#include <QContactGuid>
#include <QContactHobby>
#include <QContactName>
#include <QContactNickname>
#include <QContactNote>
#include <QContactOnlineAccount>
#include <QContactOrganization>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactRingtone>
#include <QContactSyncTarget>
#include <QContactTag>
#include <QContactTimestamp>
#include <QContactUrl>

#include <QContactDetailFilter>
#include <QContactDetailRangeFilter>
#include <QContactLocalIdFilter>
#include <QContactUnionFilter>
#include <QContactIntersectionFilter>

#include <QSqlError>
#include <QVector>

#include <QtDebug>

struct FieldInfo
{
    QLatin1String field;
    const char *column;
};

template<int N> static void setValue(
        QContactDetail *detail, const QLatin1Constant<N> &key, const QVariant &value)
{
    if (value.type() != QVariant::String || !value.toString().isEmpty())
        detail->setValue(key, value);
}

template <int N> static void setValue(
        QContactDetail *detail, const char (&key)[N], const QVariant &value)
{
    if (value.type() != QVariant::String || !value.toString().isEmpty())
        detail->setValue(key, value);
}

static const FieldInfo nameFields[] =
{
    { QContactName::FieldFirstName, "firstName" },
    { QContactName::FieldLastName, "lastName" },
    { QContactName::FieldMiddleName, "middleName" },
    { QContactName::FieldPrefix, "prefix" },
    { QContactName::FieldSuffix, "suffix" },
    { QContactName::FieldCustomLabel, "customLabel" }
};

static const FieldInfo timestampFields[] =
{
    { QContactTimestamp::FieldCreationTimestamp, "created" },
    { QContactTimestamp::FieldModificationTimestamp, "modified" }
};

static const FieldInfo genderFields[] =
{
    { QContactGender::FieldGender, "gender" }
};

static const FieldInfo favoriteFields[] =
{
    { QContactFavorite::FieldFavorite, "isFavorite" }
};

static const FieldInfo addressFields[] =
{
    { QContactAddress::FieldStreet, "street" },
    { QContactAddress::FieldPostOfficeBox, "postOfficeBox" },
    { QContactAddress::FieldRegion, "region" },
    { QContactAddress::FieldLocality, "locality" },
    { QContactAddress::FieldPostcode, "postCode" },
    { QContactAddress::FieldCountry, "country" },
    { QContactDetail::FieldContext, "context" }

};

static void setValues(QContactAddress *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAddress T;

    setValue(detail, T::FieldStreet       , query->value(offset + 0));
    setValue(detail, T::FieldPostOfficeBox, query->value(offset + 1));
    setValue(detail, T::FieldRegion       , query->value(offset + 2));
    setValue(detail, T::FieldLocality     , query->value(offset + 3));
    setValue(detail, T::FieldPostcode     , query->value(offset + 4));
    setValue(detail, T::FieldCountry      , query->value(offset + 5));
}

static const FieldInfo anniversaryFields[] =
{
    { QContactAnniversary::FieldCalendarId, "calendarId" },
    { QContactAnniversary::FieldOriginalDate, "originalDateTime" },
    { QContactAnniversary::FieldSubType, "subType" }
};

static void setValues(QContactAnniversary *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAnniversary T;

    setValue(detail, T::FieldCalendarId  , query->value(offset + 0));
    setValue(detail, T::FieldOriginalDate, query->value(offset + 1));
    setValue(detail, T::FieldSubType     , query->value(offset + 2));
}

static const FieldInfo avatarFields[] =
{
    { QContactAvatar::FieldImageUrl, "imageUrl" },
    { QContactAvatar::FieldVideoUrl, "videoUrl" }
};

static void setValues(QContactAvatar *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAvatar T;

    setValue(detail, T::FieldImageUrl, query->value(offset + 0));
    setValue(detail, T::FieldVideoUrl, query->value(offset + 1));
}

static const FieldInfo birthdayFields[] =
{
    { QContactBirthday::FieldCalendarId, "calendarId" },
    { QContactBirthday::FieldBirthday, "birthday" }
};

static void setValues(QContactBirthday *detail, QSqlQuery *query, const int offset)
{
    typedef QContactBirthday T;

    setValue(detail, T::FieldCalendarId, query->value(offset + 0));
    setValue(detail, T::FieldBirthday  , query->value(offset + 1));
}

static const FieldInfo emailAddressFields[] =
{
    { QContactEmailAddress::FieldEmailAddress, "emailAddress" },
    { QContactDetail::FieldContext, "context" }
};

static void setValues(QContactEmailAddress *detail, QSqlQuery *query, const int offset)
{
    typedef QContactEmailAddress T;

    setValue(detail, T::FieldEmailAddress, query->value(offset + 0));
}

static const FieldInfo guidFields[] =
{
    { QContactGuid::FieldGuid, "guid" }
};

static void setValues(QContactGuid *detail, QSqlQuery *query, const int offset)
{
    typedef QContactGuid T;

    setValue(detail, T::FieldGuid, query->value(offset + 0));
}

static const FieldInfo hobbyFields[] =
{
    { QContactHobby::FieldHobby, "hobby" }
};

static void setValues(QContactHobby *detail, QSqlQuery *query, const int offset)
{
    typedef QContactHobby T;

    setValue(detail, T::FieldHobby, query->value(offset + 0));
}

static const FieldInfo nicknameFields[] =
{
    { QContactNickname::FieldNickname, "nickname" }
};

static void setValues(QContactNickname *detail, QSqlQuery *query, const int offset)
{
    typedef QContactNickname T;

    setValue(detail, T::FieldNickname, query->value(offset + 0));
}

static const FieldInfo noteFields[] =
{
    { QContactNote::FieldNote, "note" }
};

static void setValues(QContactNote *detail, QSqlQuery *query, const int offset)
{
    typedef QContactNote T;

    setValue(detail, T::FieldNote, query->value(offset + 0));
}

static const FieldInfo onlineAccountFields[] =
{
    { QContactOnlineAccount::FieldAccountUri, "accountUri" },
    { QContactOnlineAccount::FieldProtocol, "protocol" },
    { QContactOnlineAccount::FieldServiceProvider, "serviceProvider" },
    { QContactOnlineAccount::FieldCapabilities, "capabilities" },
    { QContactOnlineAccount::FieldSubTypes, "subTypes" },
    { QLatin1String("AccountPath")    , "accountPath" },
    { QLatin1String("AccountIconPath"), "accountIconPath" },
    { QLatin1String("Enabled")        , "enabled" }
};

static void setValues(QContactOnlineAccount *detail, QSqlQuery *query, const int offset)
{
    typedef QContactOnlineAccount T;

    setValue(detail, T::FieldAccountUri     , query->value(offset + 0));
    setValue(detail, T::FieldProtocol       , query->value(offset + 1));
    setValue(detail, T::FieldServiceProvider, query->value(offset + 2));
    setValue(detail, T::FieldCapabilities   , query->value(offset + 3).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes       , query->value(offset + 4).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, "AccountPath"          , query->value(offset + 5));
    setValue(detail, "AccountIconPath"      , query->value(offset + 6));
    setValue(detail, "Enabled"              , query->value(offset + 7));
}

static const FieldInfo organizationFields[] =
{
    { QContactOrganization::FieldName, "name" },
    { QContactOrganization::FieldRole, "role" },
    { QContactOrganization::FieldTitle, "title" },
    { QContactOrganization::FieldLocation, "location" },
    { QContactOrganization::FieldDepartment, "department" },
    { QContactOrganization::FieldLogoUrl, "logoUrl" }
};

static void setValues(QContactOrganization *detail, QSqlQuery *query, const int offset)
{
    typedef QContactOrganization T;

    setValue(detail, T::FieldName      , query->value(offset + 0));
    setValue(detail, T::FieldRole      , query->value(offset + 1));
    setValue(detail, T::FieldTitle     , query->value(offset + 2));
    setValue(detail, T::FieldLocation  , query->value(offset + 3));
    setValue(detail, T::FieldDepartment, query->value(offset + 4).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldLogoUrl   , QUrl(query->value(offset + 5).toString()));
}

static const FieldInfo phoneNumberFields[] =
{
    { QContactPhoneNumber::FieldNumber, "phoneNumber" },
    { QContactPhoneNumber::FieldSubTypes, "subTypes" },
    { QContactPhoneNumber::FieldContext, "context" },
    { QLatin1String("NormalizedNumber"), "normalizedNumber" }
};

static void setValues(QContactPhoneNumber *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPhoneNumber T;

    setValue(detail, T::FieldNumber  , query->value(offset + 0));
    setValue(detail, T::FieldSubTypes, query->value(offset + 1).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, "NormalizedNumber", query->value(offset + 2));
}

static const FieldInfo presenceFields[] =
{
    { QContactPresence::FieldPresenceState, "presenceState" },
    { QContactPresence::FieldTimestamp, "timestamp" },
    { QContactPresence::FieldNickname, "nickname" },
    { QContactPresence::FieldCustomMessage, "customMessage" }
};

static void setValues(QContactPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0));
    setValue(detail, T::FieldTimestamp    , query->value(offset + 1));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
}

static void setValues(QContactGlobalPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0));
    setValue(detail, T::FieldTimestamp    , query->value(offset + 1));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
}

static const FieldInfo ringtoneFields[] =
{
    { QContactRingtone::FieldAudioRingtoneUrl, "audioRingtone" },
    { QContactRingtone::FieldVideoRingtoneUrl, "videoRingtone" }
};

static void setValues(QContactRingtone *detail, QSqlQuery *query, const int offset)
{
    typedef QContactRingtone T;

    setValue(detail, T::FieldAudioRingtoneUrl, QUrl(query->value(offset + 0).toString()));
    setValue(detail, T::FieldVideoRingtoneUrl, QUrl(query->value(offset + 1).toString()));
}

static const FieldInfo syncTargetFields[] =
{
    { QContactSyncTarget::FieldSyncTarget, "syncTarget" }
};

static void setValues(QContactSyncTarget *detail, QSqlQuery *query, const int offset)
{
    typedef QContactSyncTarget T;

    setValue(detail, T::FieldSyncTarget, query->value(offset + 0));
}

static const FieldInfo tagFields[] =
{
    { QContactTag::FieldTag, "street" }
};

static void setValues(QContactTag *detail, QSqlQuery *query, const int offset)
{
    typedef QContactTag T;

    setValue(detail, T::FieldTag, query->value(offset + 0));
}

static const FieldInfo urlFields[] =
{
    { QContactUrl::FieldUrl, "url" },
    { QContactUrl::FieldSubType, "subTypes" }
};

static void setValues(QContactUrl *detail, QSqlQuery *query, const int offset)
{
    typedef QContactUrl T;

    setValue(detail, T::FieldUrl    , query->value(offset + 0));
    setValue(detail, T::FieldSubType, query->value(offset + 1));
}

static const FieldInfo tpMetadataFields[] =
{
    { QLatin1String("ContactId"), "telepathyId" },
    { QLatin1String("AccountId"), "accountId" },
    { QLatin1String("AccountEnabled"), "accountEnabled" }
};

static void setValues(QContactTpMetadata *detail, QSqlQuery *query, const int offset)
{
    setValue(detail, "ContactId"     , query->value(offset + 0));
    setValue(detail, "AccountId"     , query->value(offset + 1));
    setValue(detail, "AccountEnabled", query->value(offset + 2));
}

template <typename T> static void readDetail(
        QContactLocalId contactId, QContact *contact, QSqlQuery *query, QContactLocalId &currentId)
{
    do {
        T detail;

        QString detailUriValue = query->value(0).toString();
        QString linkedDetailUrisValue = query->value(1).toString();
        QString contextValue = query->value(2).toString();

        if (!detailUriValue.isEmpty()) {
            setValue(&detail,
                     QContactDetail::FieldDetailUri,
                     detailUriValue);
        }
        if (!linkedDetailUrisValue.isEmpty()) {
            setValue(&detail,
                     QContactDetail::FieldLinkedDetailUris,
                     linkedDetailUrisValue.split(QLatin1Char(';'), QString::SkipEmptyParts));
        }
        if (!contextValue.isEmpty()) {
            setValue(&detail,
                     QContactDetail::FieldContext,
                     contextValue.split(QLatin1Char(';'), QString::SkipEmptyParts));
        }
        setValues(&detail, query, 5);

        contact->saveDetail(&detail);
    } while (query->next() && (currentId = query->value(4).toUInt()) == contactId);
}

typedef void (*ReadDetail)(QContactLocalId contactId, QContact *contact, QSqlQuery *query, QContactLocalId &currentId);

struct DetailInfo
{
    const QLatin1String detail;
    const char *table;
    const FieldInfo *fields;
    const int fieldCount;
    const bool join;
    const ReadDetail read;

    QString where() const
    {
        return table
                ? QString(QLatin1String("contactId IN (SELECT contactId FROM %1 WHERE %2)")).arg(QLatin1String(table))
                : QLatin1String("%2");
    }
};

template <typename T, int N> static int lengthOf(const T(&)[N]) { return N; }

#define DEFINE_DETAIL(Detail, Table, fields, join) \
    { Detail::DefinitionName, #Table, fields, lengthOf(fields), join, readDetail<Detail> }

#define DEFINE_DETAIL_PRIMARY_TABLE(Detail, fields) \
    { Detail::DefinitionName, 0, fields, lengthOf(fields), false, 0 }

static const DetailInfo detailInfo[] =
{
    DEFINE_DETAIL_PRIMARY_TABLE(QContactName,      nameFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactTimestamp, timestampFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactGender,    genderFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactFavorite,  favoriteFields),
    DEFINE_DETAIL(QContactAddress       , Addresses      , addressFields      , false),
    DEFINE_DETAIL(QContactAnniversary   , Anniversaries  , anniversaryFields  , false),
    DEFINE_DETAIL(QContactAvatar        , Avatars        , avatarFields       , false),
    DEFINE_DETAIL(QContactBirthday      , Birthdays      , birthdayFields     , false),
    DEFINE_DETAIL(QContactEmailAddress  , EmailAddresses , emailAddressFields , false),
    DEFINE_DETAIL(QContactGuid          , Guids          , guidFields         , false),
    DEFINE_DETAIL(QContactHobby         , Hobbies        , hobbyFields        , false),
    DEFINE_DETAIL(QContactNickname      , Nicknames      , nicknameFields     , false),
    DEFINE_DETAIL(QContactNote          , Notes          , noteFields         , false),
    DEFINE_DETAIL(QContactOnlineAccount , OnlineAccounts , onlineAccountFields, false),
    DEFINE_DETAIL(QContactOrganization  , Organizations  , organizationFields , false),
    DEFINE_DETAIL(QContactPhoneNumber   , PhoneNumbers   , phoneNumberFields  , false),
    DEFINE_DETAIL(QContactPresence      , Presences      , presenceFields     , false),
    DEFINE_DETAIL(QContactRingtone      , Ringtones      , ringtoneFields     , false),
    DEFINE_DETAIL(QContactSyncTarget    , SyncTargets    , syncTargetFields   , false),
    DEFINE_DETAIL(QContactTag           , Tags           , tagFields          , false),
    DEFINE_DETAIL(QContactUrl           , Urls           , urlFields          , false),
    DEFINE_DETAIL(QContactTpMetadata    , TpMetadata     , tpMetadataFields   , false),
    DEFINE_DETAIL(QContactGlobalPresence, GlobalPresences, presenceFields     , true)
};

static QString buildWhere(const QContactDetailFilter &filter, QVariantList *bindings)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (detail.detail != filter.detailDefinitionName())
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];
            if (field.field != filter.detailFieldName())
                continue;
            // ### MatchFlags
            const QString comparison(QLatin1String("%1 = ?"));
            bindings->append(filter.value());
            return detail.where().arg(comparison.arg(field.column));
        }
    }
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactDetailRangeFilter &filter, QVariantList *bindings)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (detail.detail != filter.detailDefinitionName())
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];
            if (field.field != filter.detailFieldName())
                continue;
            bindings->append(filter.minValue());
            bindings->append(filter.maxValue());
            QString comparison = (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeUpper)
                    ? QLatin1String("%1 > ?")
                    : QLatin1String("%1 <= ?");
            comparison += (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeLower)
                    ? QLatin1String("&& %1 < ?")
                    : QLatin1String("&& %1 >= ?");
            return detail.where().arg(comparison.arg(field.column));
        }
    }
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactLocalIdFilter &filter, QVariantList *bindings)
{
    const QList<QContactLocalId> contactIds = filter.ids();
    if (contactIds.isEmpty())
        return QLatin1String("FALSE");

    QString statement = QLatin1String("contactId IN (?");
    bindings->append(contactIds.first() - 1);

    for (int i = 1; i < contactIds.count(); ++i) {
        statement += QLatin1String(",?");
        bindings->append(contactIds.at(i) - 1);
    }
    return statement + QLatin1String(")");
}

static QString buildWhere(const QContactFilter &filter, QVariantList *bindings);

static QString buildWhere(const QContactUnionFilter &filter, QVariantList *bindings)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();
    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, bindings);
        fragments.append(!fragment.isEmpty() ? fragment : QLatin1String("TRUE"));
    }
    return fragments.join(QLatin1String(" || "));
}

static QString buildWhere(const QContactIntersectionFilter &filter, QVariantList *bindings)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();
    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, bindings);
        fragments.append(!fragment.isEmpty() ? fragment : QLatin1String("TRUE"));
    }
    return fragments.join(QLatin1String(" && "));
}

static QString buildWhere(const QContactFilter &filter, QVariantList *bindings)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
        return QString();
    case QContactFilter::ContactDetailFilter:
        return buildWhere(QContactDetailFilter(filter), bindings);
    case QContactFilter::ContactDetailRangeFilter:
        return buildWhere(QContactDetailRangeFilter(filter), bindings);
    case QContactFilter::IntersectionFilter:
        return buildWhere(QContactIntersectionFilter(filter), bindings);
    case QContactFilter::UnionFilter:
        return buildWhere(QContactUnionFilter(filter), bindings);
    case QContactFilter::LocalIdFilter:
        return buildWhere(QContactLocalIdFilter(filter), bindings);
    default:
        return QLatin1String("FALSE");
    }
}

static QString buildOrderBy(const QContactSortOrder &order, QStringList *joins)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (detail.detail != order.detailDefinitionName())
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];
            if (field.field != order.detailFieldName())
                continue;

            if (detail.join) {
                QString join = QString(QLatin1String(
                        "LEFT JOIN %1 ON Contacts.contactId = %1.contactId"))
                        .arg(QLatin1String(detail.table));

                if (!joins->contains(join))
                    joins->append(join);

                return QString(QLatin1String("%1.%2"))
                        .arg(QLatin1String(detail.table))
                        .arg(QLatin1String(field.column));
            } else if (!detail.table) {
                return QLatin1String(field.column);
            }
        }
    }
    return QString();
}

static QString buildOrderBy(const QList<QContactSortOrder> &order, QString *join)
{
    QStringList joins;
    QStringList fragments;
    foreach (const QContactSortOrder &sort, order) {
        fragments.append(buildOrderBy(sort, &joins));
    }

    *join = joins.join(QLatin1String(" "));

    fragments.append(QLatin1String("displayLabel"));
    return fragments.join(QLatin1String(", "));
}

ContactReader::ContactReader(const QSqlDatabase &database)
    : m_database(database)
{
}

ContactReader::~ContactReader()
{
}

struct Table
{
    QSqlQuery query;
    ReadDetail read;
    QContactLocalId currentId;
};

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order,
        const QStringList &details)
{
    if (!m_database.transaction()) {
        return QContactManager::UnspecifiedError;
    }

    QString join;

    QVariantList bindings;
    QString where = buildWhere(filter, &bindings);
    if (!where.isEmpty())
        where = QLatin1String("WHERE ") + where;
    const QString orderBy = buildOrderBy(order, &join);

    const QString queryString = QString(QLatin1String(
                "\n CREATE TABLE temp.%1 AS"
                "\n SELECT contactId"
                "\n FROM Contacts %2"
                "\n %3"
                "\n ORDER BY %4;")).arg(table).arg(join).arg(where).arg(orderBy);

    QSqlQuery query(m_database);
    if (!query.prepare(queryString)) {
        qWarning() << "Failed to prepare contacts";
        qWarning() << query.lastError();
        qWarning() << queryString;
        m_database.rollback();
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!query.exec()) {
        qWarning() << "Failed to query contacts";
        qWarning() << query.lastError();
        qWarning() << queryString;
        m_database.rollback();
        return QContactManager::UnspecifiedError;
    }

    const QContactManager::Error error = queryContacts(table, contacts, details);

    QSqlQuery dropQuery(m_database);
    const QString dropQueryString = QString(QLatin1String("DROP TABLE temp.%1;")).arg(table);
    if (!dropQuery.prepare(dropQueryString)) {
        qWarning() << "Failed to prepare drop temporary table" << table << "query";
        qWarning() << dropQuery.lastError();
        qWarning() << dropQueryString;
    } else {
        if (!dropQuery.exec()) {
            qWarning() << "Failed to drop temporary table" << table;
            qWarning() << dropQuery.lastError();
            qWarning() << dropQueryString;
        } else {
            // success.
            m_database.commit();
            return error;
        }
    }

    m_database.rollback();
    return error;
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<QContactLocalId> &contactIds,
        const QStringList &details)
{
    if (!m_database.transaction()) {
        return QContactManager::UnspecifiedError;
    }

    const QString createStatement = QString(QLatin1String(
            "\n CREATE TABLE temp.%1 ("
            "\n contactId INTEGER);")).arg(table);

    QSqlQuery tableQuery(m_database);
    if (!tableQuery.prepare(createStatement)) {
        qWarning() << "Failed to prepare contacts";
        qWarning() << tableQuery.lastError();
        qWarning() << createStatement;
        m_database.rollback();
        return QContactManager::UnspecifiedError;
    }

    if (!tableQuery.exec()) {
        qWarning() << "Failed to query contacts";
        qWarning() << tableQuery.lastError();
        qWarning() << createStatement;
        m_database.rollback();
        return QContactManager::UnspecifiedError;
    }

    QSqlQuery insertQuery(m_database);
    insertQuery.prepare(QString(QLatin1String(
        "\n INSERT INTO %1 ("
        "\n  contactId)"
        "\n VALUES("
        "\n  :contactId);")).arg(table));

    QVariantList boundIds;
    foreach (const QContactLocalId &contactId, contactIds)
        boundIds.append(contactId - 1);

    insertQuery.bindValue(0, boundIds);

    QContactManager::Error error = insertQuery.execBatch()
            ? QContactManager::NoError
            : QContactManager::UnspecifiedError;

    if (error != QContactManager::NoError) {
        qWarning() << "Failed to cache contact IDs";
        qWarning() << insertQuery.lastError();
        m_database.rollback();
        return error;
    } else {
        error = queryContacts(table, contacts, details);
        if (contacts && (contacts->size() != contactIds.size())) {
            error = QContactManager::DoesNotExistError;
        }
    }

    QSqlQuery dropQuery(m_database);
    const QString dropQueryString = QString(QLatin1String("DROP TABLE temp.%1;")).arg(table);
    if (!dropQuery.prepare(dropQueryString)) {
        qWarning() << "Failed to prepare drop temporary table" << table << "query";
        qWarning() << dropQuery.lastError();
        qWarning() << dropQueryString;
    } else {
        if (!dropQuery.exec()) {
            qWarning() << "Failed to drop temporary table" << table;
            qWarning() << dropQuery.lastError();
            qWarning() << dropQueryString;
        } else {
            m_database.commit();
            return error;
        }
    }

    m_database.rollback();
    return error;
}

QContactManager::Error ContactReader::queryContacts(
        const QString &table, QList<QContact> *contacts, const QStringList &details)
{
    QSqlQuery query(m_database);
    if (!query.exec(QString(QLatin1String(
            "\n SELECT Contacts.*"
            "\n FROM %1 INNER JOIN Contacts ON %1.contactId = Contacts.contactId;")).arg(table))) {
        qWarning() << "Failed to query from" << table;
        qWarning() << query.lastError();
        return QContactManager::UnspecifiedError;
    }

    const QString tableTemplate = QString(QLatin1String(
            "\n SELECT"
            "\n  Details.detailUri,"
            "\n  Details.linkedDetailUris,"
            "\n  Details.contexts,"
            "\n  %2.*"
            "\n FROM %1"
            "\n  INNER JOIN %2 ON %1.contactId = %2.contactId"
            "\n  LEFT JOIN Details ON %2.detailId = Details.detailId AND Details.detail = :detail;")).arg(table);

    QVector<Table> tables;
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!detail.read)
            continue;

        if (details.isEmpty() || details.contains(detail.detail)) {
            Table table = {
                QSqlQuery(m_database),
                detail.read,
                0
            };

            if (!table.query.prepare(tableTemplate.arg(QLatin1String(detail.table)))) {
                qWarning() << "Failed to prepare table" << detail.table;
                qWarning() << table.query.lastError();
            } else {
                table.query.bindValue(0, detail.detail);

                if (!table.query.exec()) {
                    qWarning() << "Failed to query table" << detail.table;
                    qWarning() << table.query.lastError();
                } else if (table.query.next()) {
                    table.currentId = table.query.value(4).toUInt();
                    tables.append(table);
                }
            }
        }
    }

    do {
        for (int i = 0; i < 50 && query.next(); ++i) {
            QContactLocalId contactId = query.value(0).toUInt();
            QContact contact;

            QContactId id;
            id.setLocalId(contactId + 1);
            id.setManagerUri(QLatin1String("org.nemomobile.contacts.sqlite"));
            contact.setId(id);

            QContactName name;
            setValue(&name, QContactName::FieldFirstName  , query.value(2));
            setValue(&name, QContactName::FieldLastName   , query.value(3));
            setValue(&name, QContactName::FieldMiddleName , query.value(4));
            setValue(&name, QContactName::FieldPrefix     , query.value(5));
            setValue(&name, QContactName::FieldSuffix     , query.value(6));
            setValue(&name, QContactName::FieldCustomLabel, query.value(7));
            if (!name.isEmpty())
                contact.saveDetail(&name);

            QContactTimestamp timestamp;
            setValue(&timestamp, QContactTimestamp::FieldCreationTimestamp    , query.value(8));
            setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, query.value(9));
            if (!timestamp.isEmpty())
                contact.saveDetail(&timestamp);

            QContactGender gender;
            setValue(&gender, QContactGender::FieldGender, query.value(10));
            if (!gender.isEmpty())
                contact.saveDetail(&gender);

            QContactFavorite favorite;
            setValue(&favorite, QContactFavorite::FieldFavorite, query.value(11));
            if (!favorite.isEmpty())
                contact.saveDetail(&favorite);

            for (int j = 0; j < tables.count(); ++j) {
                Table &table = tables[j];

                if (table.query.isValid() && table.currentId == contactId)
                    table.read(contactId, &contact, &table.query, table.currentId);
            }
            contacts->append(contact);
        }
        contactsAvailable(*contacts);
    } while (query.isValid());

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readContactIds(
        QList<QContactLocalId> *contactIds,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order)
{
    QString join;

    QVariantList bindings;
    QString where = buildWhere(filter, &bindings);
    if (!where.isEmpty())
        where = QLatin1String("WHERE ") + where;
    const QString orderBy = buildOrderBy(order, &join);

    const QString queryString = QString(QLatin1String(
                "\n SELECT contactId"
                "\n FROM Contacts %1"
                "\n %2"
                "\n ORDER BY %3;")).arg(join).arg(where).arg(orderBy);

    QSqlQuery query(m_database);
    if (!query.prepare(queryString)) {
        qWarning() << "Failed to prepare contacts ids";
        qWarning() << query.lastError();
        qWarning() << queryString;
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!query.exec()) {
        qWarning() << "Failed to prepare contacts ids";
        qWarning() << query.lastError();
        qWarning() << queryString;
        return QContactManager::UnspecifiedError;
    }

    do {
        for (int i = 0; i < 50 && query.next(); ++i) {
            contactIds->append(query.value(0).toUInt() + 1);
        }
        contactIdsAvailable(*contactIds);
    } while (query.isValid());
    return QContactManager::NoError;
}

QContactManager::Error ContactReader::getIdentity(
        ContactsDatabase::Identity identity, QContactLocalId *contactId)
{
    QSqlQuery query(m_database);
    query.prepare(QLatin1String(
            "\n SELECT contactId"
            "\n FROM Identities"
            "\n WHERE identity = :identity"));
    query.bindValue(0, identity);

    if (!query.exec()) {
        *contactId = 0;
        return QContactManager::UnspecifiedError;
    }

    *contactId = query.next()
            ? (query.value(0).toUInt() + 1)
            : 0;

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readRelationships(
        QList<QContactRelationship> *relationships,
        const QString &type,
        const QContactId &first,
        const QContactId &second)
{
    QStringList whereStatements;
    QVariantList bindings;
    if (!type.isEmpty()) {
        whereStatements.append(QLatin1String("type = ?"));
        bindings.append(type);
    }

    if (first.localId() != 0) {
        whereStatements.append(QLatin1String("firstId = ?"));
        bindings.append(first.localId() - 1);
    }

    if (second.localId() != 0) {
        whereStatements.append(QLatin1String("secondId = ?"));
        bindings.append(second.localId() - 1);
    }

    const QString where = !whereStatements.isEmpty()
            ? QLatin1String("\n WHERE ") + whereStatements.join(QLatin1String(" AND "))
            : QString();

    QString statement = QLatin1String(
            "\n SELECT type, firstId, secondId"
            "\n FROM Relationships") + where;

    QSqlQuery query(m_database);
    if (query.prepare(statement)) {
        qWarning() << "Failed to prepare relationships query";
        qWarning() << query.lastError();
        qWarning() << statement;
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!query.exec()) {
        qWarning() << "Failed to query relationships";
        qWarning() << query.lastError();
        return QContactManager::UnspecifiedError;
    }

    while (query.next()) {
        QContactRelationship relationship;
        relationship.setRelationshipType(query.value(0).toString());

        QContactId firstId;
        firstId.setLocalId(query.value(1).toUInt() + 1);
        relationship.setFirst(firstId);

        QContactId secondId;
        secondId.setLocalId(query.value(2).toUInt() + 1);
        relationship.setFirst(secondId);

        relationships->append(relationship);
    }

    return QContactManager::NoError;
}

void ContactReader::contactsAvailable(const QList<QContact> &)
{
}

void ContactReader::contactIdsAvailable(const QList<QContactLocalId> &)
{
}
