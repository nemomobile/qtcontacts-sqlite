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
#include "contactsengine.h"
#include "conversion_p.h"
#include "trace_p.h"

#include "../extensions/qtcontacts-extensions.h"
#include "../extensions/qcontactdeactivated.h"
#include "../extensions/qcontactincidental.h"
#include "../extensions/qcontactoriginmetadata.h"
#include "../extensions/qcontactstatusflags.h"

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
#include <QContactIdFilter>
#include <QContactChangeLogFilter>
#include <QContactUnionFilter>
#include <QContactIntersectionFilter>

#include <QContactManagerEngine>

#include <QSqlError>
#include <QVector>

#include <QtDebug>

using namespace Conversion;

static const int ReportBatchSize = 50;

static const QString aggregateSyncTarget(QString::fromLatin1("aggregate"));
static const QString localSyncTarget(QString::fromLatin1("local"));
static const QString wasLocalSyncTarget(QString::fromLatin1("was_local"));

enum FieldType {
    StringField = 0,
    StringListField,
    IntegerField,
    DateField,
    BooleanField,
    OtherField
};

struct FieldInfo
{
    int field;
    const char *column;
    FieldType fieldType;
};

static void setValue(QContactDetail *detail, int key, const QVariant &value)
{
    if (value.type() != QVariant::String || !value.toString().isEmpty())
        detail->setValue(key, value);
}

static QVariant stringListValue(const QVariant &columnValue)
{
    if (columnValue.isNull())
        return columnValue;

    QString listString(columnValue.toString());
    return listString.split(QLatin1Char(';'), QString::SkipEmptyParts);
}

static QVariant urlValue(const QVariant &columnValue)
{
    if (columnValue.isNull())
        return columnValue;

    QString urlString(columnValue.toString());
    return QUrl(urlString);
}

static QVariant dateValue(const QVariant &columnValue)
{
    if (columnValue.isNull())
        return columnValue;

    QString dtString(columnValue.toString());
    return QDate::fromString(dtString, Qt::ISODate);
}

static const FieldInfo displayLabelFields[] =
{
    { QContactDisplayLabel::FieldLabel, "displayLabel", StringField }
};

static const FieldInfo nameFields[] =
{
    { QContactName::FieldFirstName, "firstName", StringField },
    { QContactName::FieldLastName, "lastName", StringField },
    { QContactName::FieldMiddleName, "middleName", StringField },
    { QContactName::FieldPrefix, "prefix", StringField },
    { QContactName::FieldSuffix, "suffix", StringField },
    { QContactName__FieldCustomLabel, "customLabel", StringField }
};

static const FieldInfo syncTargetFields[] =
{
    { QContactSyncTarget::FieldSyncTarget, "syncTarget", StringField }
};

static const FieldInfo timestampFields[] =
{
    { QContactTimestamp::FieldCreationTimestamp, "created", DateField },
    { QContactTimestamp::FieldModificationTimestamp, "modified", DateField }
};

static const FieldInfo genderFields[] =
{
    { QContactGender::FieldGender, "gender", StringField }
};

static const FieldInfo favoriteFields[] =
{
    { QContactFavorite::FieldFavorite, "isFavorite", BooleanField }
};

static const FieldInfo statusFlagsFields[] =
{
    // No specific field; tests hasPhoneNumber/hasEmailAddress/hasOnlineAccount/isOnline/isDeactivated/isIncidental
    { QContactStatusFlags::FieldFlags, "", OtherField }
};

static const FieldInfo typeFields[] =
{
    { QContactType::FieldType, "type", IntegerField }
};

static const FieldInfo addressFields[] =
{
    { QContactAddress::FieldStreet, "street", StringField },
    { QContactAddress::FieldPostOfficeBox, "postOfficeBox", StringField },
    { QContactAddress::FieldRegion, "region", StringField },
    { QContactAddress::FieldLocality, "locality", StringField },
    { QContactAddress::FieldPostcode, "postCode", StringField },
    { QContactAddress::FieldCountry, "country", StringField },
    { QContactAddress::FieldSubTypes, "subTypes", StringListField },
    { QContactDetail::FieldContext, "context", StringField }

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
    QStringList subTypeNames(query->value(offset + 6).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes     , QVariant::fromValue<QList<int> >(Address::subTypeList(subTypeNames)));

}

static const FieldInfo anniversaryFields[] =
{
    { QContactAnniversary::FieldOriginalDate, "originalDateTime", DateField },
    { QContactAnniversary::FieldCalendarId, "calendarId", StringField },
    { QContactAnniversary::FieldSubType, "subType", StringField }
};

static void setValues(QContactAnniversary *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAnniversary T;

    setValue(detail, T::FieldOriginalDate, dateValue(query->value(offset + 0)));
    setValue(detail, T::FieldCalendarId  , query->value(offset + 1));
    setValue(detail, T::FieldSubType     , QVariant::fromValue<int>(Anniversary::subType(query->value(offset + 2).toString())));
}

static const FieldInfo avatarFields[] =
{
    { QContactAvatar::FieldImageUrl, "imageUrl", StringField },
    { QContactAvatar::FieldVideoUrl, "videoUrl", StringField },
    { QContactAvatar__FieldAvatarMetadata, "avatarMetadata", StringField }
};

static void setValues(QContactAvatar *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAvatar T;

    setValue(detail, T::FieldImageUrl, urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldVideoUrl, urlValue(query->value(offset + 1)));
    setValue(detail, QContactAvatar__FieldAvatarMetadata, query->value(offset + 2));
}

static const FieldInfo birthdayFields[] =
{
    { QContactBirthday::FieldBirthday, "birthday", DateField },
    { QContactBirthday::FieldCalendarId, "calendarId", StringField }
};

static void setValues(QContactBirthday *detail, QSqlQuery *query, const int offset)
{
    typedef QContactBirthday T;

    setValue(detail, T::FieldBirthday  , dateValue(query->value(offset + 0)));
    setValue(detail, T::FieldCalendarId, query->value(offset + 1));
}

static const FieldInfo emailAddressFields[] =
{
    { QContactEmailAddress::FieldEmailAddress, "emailAddress", StringField },
    { QContactDetail::FieldContext, "context", StringField }
};

static void setValues(QContactEmailAddress *detail, QSqlQuery *query, const int offset)
{
    typedef QContactEmailAddress T;

    setValue(detail, T::FieldEmailAddress, query->value(offset + 0));
    // ignore lowerEmailAddress
}

static const FieldInfo guidFields[] =
{
    { QContactGuid::FieldGuid, "guid", StringField }
};

static void setValues(QContactGuid *detail, QSqlQuery *query, const int offset)
{
    typedef QContactGuid T;

    setValue(detail, T::FieldGuid, query->value(offset + 0));
}

static const FieldInfo hobbyFields[] =
{
    { QContactHobby::FieldHobby, "hobby", StringField }
};

static void setValues(QContactHobby *detail, QSqlQuery *query, const int offset)
{
    typedef QContactHobby T;

    setValue(detail, T::FieldHobby, query->value(offset + 0));
}

static const FieldInfo nicknameFields[] =
{
    { QContactNickname::FieldNickname, "nickname", StringField }
};

static void setValues(QContactNickname *detail, QSqlQuery *query, const int offset)
{
    typedef QContactNickname T;

    setValue(detail, T::FieldNickname, query->value(offset + 0));
    // ignore lowerNickname
}

static const FieldInfo noteFields[] =
{
    { QContactNote::FieldNote, "note", StringField }
};

static void setValues(QContactNote *detail, QSqlQuery *query, const int offset)
{
    typedef QContactNote T;

    setValue(detail, T::FieldNote, query->value(offset + 0));
}

static const FieldInfo onlineAccountFields[] =
{
    { QContactOnlineAccount::FieldAccountUri, "accountUri", StringField },
    { QContactOnlineAccount::FieldProtocol, "protocol", StringField },
    { QContactOnlineAccount::FieldServiceProvider, "serviceProvider", StringField },
    { QContactOnlineAccount::FieldCapabilities, "capabilities", StringListField },
    { QContactOnlineAccount::FieldSubTypes, "subTypes", StringListField },
    { QContactOnlineAccount__FieldAccountPath, "accountPath", StringField },
    { QContactOnlineAccount__FieldAccountIconPath, "accountIconPath", StringField },
    { QContactOnlineAccount__FieldEnabled, "enabled", BooleanField },
    { QContactOnlineAccount__FieldAccountDisplayName, "accountDisplayName", StringField },
    { QContactOnlineAccount__FieldServiceProviderDisplayName, "serviceProviderDisplayName", StringField }
};

static void setValues(QContactOnlineAccount *detail, QSqlQuery *query, const int offset)
{
    typedef QContactOnlineAccount T;

    setValue(detail, T::FieldAccountUri     , query->value(offset + 0));
    // ignore lowerAccountUri
    setValue(detail, T::FieldProtocol       , QVariant::fromValue<int>(OnlineAccount::protocol(query->value(offset + 2).toString())));
    setValue(detail, T::FieldServiceProvider, query->value(offset + 3));
    setValue(detail, T::FieldCapabilities   , stringListValue(query->value(offset + 4)));

    QStringList subTypeNames(query->value(offset + 5).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(OnlineAccount::subTypeList(subTypeNames)));

    setValue(detail, QContactOnlineAccount__FieldAccountPath,                query->value(offset + 6));
    setValue(detail, QContactOnlineAccount__FieldAccountIconPath,            query->value(offset + 7));
    setValue(detail, QContactOnlineAccount__FieldEnabled,                    query->value(offset + 8));
    setValue(detail, QContactOnlineAccount__FieldAccountDisplayName,         query->value(offset + 9));
    setValue(detail, QContactOnlineAccount__FieldServiceProviderDisplayName, query->value(offset + 10));
}

static const FieldInfo organizationFields[] =
{
    { QContactOrganization::FieldName, "name", StringField },
    { QContactOrganization::FieldRole, "role", StringField },
    { QContactOrganization::FieldTitle, "title", StringField },
    { QContactOrganization::FieldLocation, "location", StringField },
    { QContactOrganization::FieldDepartment, "department", StringField },
    { QContactOrganization::FieldLogoUrl, "logoUrl", StringField }
};

static void setValues(QContactOrganization *detail, QSqlQuery *query, const int offset)
{
    typedef QContactOrganization T;

    setValue(detail, T::FieldName      , query->value(offset + 0));
    setValue(detail, T::FieldRole      , query->value(offset + 1));
    setValue(detail, T::FieldTitle     , query->value(offset + 2));
    setValue(detail, T::FieldLocation  , query->value(offset + 3));
    setValue(detail, T::FieldDepartment, stringListValue(query->value(offset + 4)));
    setValue(detail, T::FieldLogoUrl   , urlValue(query->value(offset + 5)));
}

static const FieldInfo phoneNumberFields[] =
{
    { QContactPhoneNumber::FieldNumber, "phoneNumber", StringField },
    { QContactPhoneNumber::FieldSubTypes, "subTypes", StringListField },
    { QContactPhoneNumber__FieldNormalizedNumber, "normalizedNumber", StringField }
};

static void setValues(QContactPhoneNumber *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPhoneNumber T;

    setValue(detail, T::FieldNumber  , query->value(offset + 0));

    QStringList subTypeNames(query->value(offset + 1).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(PhoneNumber::subTypeList(subTypeNames)));

    setValue(detail, QContactPhoneNumber__FieldNormalizedNumber, query->value(offset + 2));
}

static const FieldInfo presenceFields[] =
{
    { QContactPresence::FieldPresenceState, "presenceState", IntegerField },
    { QContactPresence::FieldTimestamp, "timestamp", DateField },
    { QContactPresence::FieldNickname, "nickname", StringField },
    { QContactPresence::FieldCustomMessage, "customMessage", StringField }
};

static void setValues(QContactPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0).toInt());
    setValue(detail, T::FieldTimestamp    , ContactsDatabase::fromDateTimeString(query->value(offset + 1).toString()));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
}

static void setValues(QContactGlobalPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0).toInt());
    setValue(detail, T::FieldTimestamp    , ContactsDatabase::fromDateTimeString(query->value(offset + 1).toString()));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
}

static const FieldInfo ringtoneFields[] =
{
    { QContactRingtone::FieldAudioRingtoneUrl, "audioRingtone", StringField },
    { QContactRingtone::FieldVideoRingtoneUrl, "videoRingtone", StringField }
};

static void setValues(QContactRingtone *detail, QSqlQuery *query, const int offset)
{
    typedef QContactRingtone T;

    setValue(detail, T::FieldAudioRingtoneUrl, urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldVideoRingtoneUrl, urlValue(query->value(offset + 1)));
}

static const FieldInfo tagFields[] =
{
    { QContactTag::FieldTag, "tag", StringField }
};

static void setValues(QContactTag *detail, QSqlQuery *query, const int offset)
{
    typedef QContactTag T;

    setValue(detail, T::FieldTag, query->value(offset + 0));
}

static const FieldInfo urlFields[] =
{
    { QContactUrl::FieldUrl, "url", StringField },
    { QContactUrl::FieldSubType, "subTypes", StringListField }
};

static void setValues(QContactUrl *detail, QSqlQuery *query, const int offset)
{
    typedef QContactUrl T;

    setValue(detail, T::FieldUrl    , urlValue(query->value(offset + 0)));
    setValue(detail, T::FieldSubType, QVariant::fromValue<int>(Url::subType(query->value(offset + 1).toString())));
}

static const FieldInfo tpMetadataFields[] =
{
    { QContactOriginMetadata::FieldId, "telepathyId", StringField },
    { QContactOriginMetadata::FieldGroupId, "accountId", StringField },
    { QContactOriginMetadata::FieldEnabled, "accountEnabled", BooleanField }
};

static void setValues(QContactOriginMetadata *detail, QSqlQuery *query, const int offset)
{
    setValue(detail, QContactOriginMetadata::FieldId     , query->value(offset + 0));
    setValue(detail, QContactOriginMetadata::FieldGroupId, query->value(offset + 1));
    setValue(detail, QContactOriginMetadata::FieldEnabled, query->value(offset + 2));
}

static const FieldInfo extendedDetailFields[] =
{
    { QContactExtendedDetail::FieldName, "name", StringField },
    { QContactExtendedDetail::FieldData, "data", OtherField }
};

static void setValues(QContactExtendedDetail *detail, QSqlQuery *query, const int offset)
{
    setValue(detail, QContactExtendedDetail::FieldName, query->value(offset + 0));
    setValue(detail, QContactExtendedDetail::FieldData, query->value(offset + 1));
}

static QMap<QString, int> contextTypes()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Home"), QContactDetail::ContextHome);
    rv.insert(QString::fromLatin1("Work"), QContactDetail::ContextWork);
    rv.insert(QString::fromLatin1("Other"), QContactDetail::ContextOther);

    return rv;
}

static int contextType(const QString &type)
{
    static const QMap<QString, int> types(contextTypes());

    QMap<QString, int>::const_iterator it = types.find(type);
    if (it != types.end()) {
        return *it;
    }
    return -1;
}

template <typename T> static void readDetail(quint32 contactId, quint32 &currentId, QContact *contact, QSqlQuery *query, bool syncable, bool relaxConstraints, bool append)
{
    do {
        if (!append) {
            // We still need to read the table rows, to get to the following details
            continue;
        }

        T detail;

        const QString detailUriValue = query->value(0).toString();
        const QString linkedDetailUrisValue = query->value(1).toString();
        const QString contextValue = query->value(2).toString();
        const int accessConstraints = query->value(3).toInt();
        const QString provenance = query->value(4).toString();
        const bool modifiable = query->value(5).toBool();
        const bool nonexportable = query->value(6).toBool();
        /* Unused:
        const quint32 detailId = query->value(7).toUInt();
        const quint32 detailContactId = query->value(8).toUInt();
        */

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
            QList<int> contexts;
            foreach (const QString &context, contextValue.split(QLatin1Char(';'), QString::SkipEmptyParts)) {
                int type = contextType(context);
                if (type != -1) {
                    contexts.append(type);
                }
            }
            if (!contexts.isEmpty()) {
                detail.setContexts(contexts);
            }
        }

        setValue(&detail, QContactDetail__FieldProvenance, provenance);

        // Only report modifiable state for non-local, non-aggregate contacts
        if (syncable) {
            setValue(&detail, QContactDetail__FieldModifiable, modifiable);
        }

        // Only include non-exportable if it is set
        if (nonexportable) {
            setValue(&detail, QContactDetail__FieldNonexportable, nonexportable);
        }

        // Constraints should be applied unless generating a partial aggregate; the partial aggregate
        // is intended for modification, so adding constraints prevents it from being used correctly
        if (!relaxConstraints) {
            QContactManagerEngine::setDetailAccessConstraints(&detail, static_cast<QContactDetail::AccessConstraints>(accessConstraints));
        }

        setValues(&detail, query, 9);

        contact->saveDetail(&detail);
    } while (query->next() && (currentId = query->value(8).toUInt()) == contactId);
}

static QContactRelationship makeRelationship(const QString &type, quint32 firstId, quint32 secondId)
{
    QContactRelationship relationship;
    relationship.setRelationshipType(type);

    QContact first, second;
    first.setId(ContactId::apiId(firstId));
    second.setId(ContactId::apiId(secondId));
    relationship.setFirst(first);
    relationship.setSecond(second);

    return relationship;
}

typedef void (*ReadDetail)(quint32 contactId,
                           quint32 &currentId,
                           QContact *contact,
                           QSqlQuery *query,
                           bool syncable,
                           bool relaxConstraints,
                           bool append);

struct DetailInfo
{
    QContactDetail::DetailType detailType;
    const char *detailName;
    const char *table;
    const FieldInfo *fields;
    const int fieldCount;
    const bool join;
    const ReadDetail read;

    QString where() const
    {
        return table
                ? QString(QLatin1String("Contacts.contactId IN (SELECT contactId FROM %1 WHERE %2)")).arg(QLatin1String(table))
                : QLatin1String("%2");
    }
};

template <typename T, int N> static int lengthOf(const T(&)[N]) { return N; }

template <typename T> QContactDetail::DetailType detailIdentifier() { return T::Type; }

#define PREFIX_LENGTH 8
#define DEFINE_DETAIL(Detail, Table, fields, join) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, #Table, fields, lengthOf(fields), join, readDetail<Detail> }

#define DEFINE_DETAIL_PRIMARY_TABLE(Detail, fields) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, 0, fields, lengthOf(fields), false, 0 }

// Note: join should be true only if there can be only a single row for each contact in that table
static const DetailInfo detailInfo[] =
{
    DEFINE_DETAIL_PRIMARY_TABLE(QContactDisplayLabel, displayLabelFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactName,         nameFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactSyncTarget,   syncTargetFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactTimestamp,    timestampFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactGender,       genderFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactFavorite,     favoriteFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactStatusFlags,  statusFlagsFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactType,         typeFields),
    DEFINE_DETAIL(QContactAddress       , Addresses      , addressFields       , false),
    DEFINE_DETAIL(QContactAnniversary   , Anniversaries  , anniversaryFields   , false),
    DEFINE_DETAIL(QContactAvatar        , Avatars        , avatarFields        , false),
    DEFINE_DETAIL(QContactBirthday      , Birthdays      , birthdayFields      , true),
    DEFINE_DETAIL(QContactEmailAddress  , EmailAddresses , emailAddressFields  , false),
    DEFINE_DETAIL(QContactGuid          , Guids          , guidFields          , true),
    DEFINE_DETAIL(QContactHobby         , Hobbies        , hobbyFields         , false),
    DEFINE_DETAIL(QContactNickname      , Nicknames      , nicknameFields      , false),
    DEFINE_DETAIL(QContactNote          , Notes          , noteFields          , false),
    DEFINE_DETAIL(QContactOnlineAccount , OnlineAccounts , onlineAccountFields , false),
    DEFINE_DETAIL(QContactOrganization  , Organizations  , organizationFields  , false),
    DEFINE_DETAIL(QContactPhoneNumber   , PhoneNumbers   , phoneNumberFields   , false),
    DEFINE_DETAIL(QContactPresence      , Presences      , presenceFields      , false),
    DEFINE_DETAIL(QContactRingtone      , Ringtones      , ringtoneFields      , false),
    DEFINE_DETAIL(QContactTag           , Tags           , tagFields           , false),
    DEFINE_DETAIL(QContactUrl           , Urls           , urlFields           , false),
    DEFINE_DETAIL(QContactOriginMetadata, TpMetadata     , tpMetadataFields    , true),
    DEFINE_DETAIL(QContactGlobalPresence, GlobalPresences, presenceFields      , true),
    DEFINE_DETAIL(QContactExtendedDetail, ExtendedDetails, extendedDetailFields, false),
};

#undef DEFINE_DETAIL_PRIMARY_TABLE
#undef DEFINE_DETAIL
#undef PREFIX_LENGTH

static QString fieldName(const char *table, const char *field)
{
    return QString::fromLatin1(table ? table : "Contacts").append(QChar('.')).append(QString::fromLatin1(field));
}

static QHash<QString, QString> getCaseInsensitiveColumnNames()
{
    QHash<QString, QString> names;
    names.insert(fieldName("Contacts", "firstName"), QString::fromLatin1("lowerFirstName"));
    names.insert(fieldName("Contacts", "lastName"), QString::fromLatin1("lowerLastName"));
    names.insert(fieldName("EmailAddresses", "emailAddress"), QString::fromLatin1("lowerEmailAddress"));
    names.insert(fieldName("OnlineAccounts", "accountUri"), QString::fromLatin1("lowerAccountUri"));
    names.insert(fieldName("Nicknames", "nickname"), QString::fromLatin1("lowerNickname"));
    return names;
}

static QString caseInsensitiveColumnName(const char *table, const char *column)
{
    static QHash<QString, QString> columnNames(getCaseInsensitiveColumnNames());
    return columnNames.value(fieldName(table, column));
}

static QString dateString(const DetailInfo &detail, const QDateTime &qdt)
{
    if (detail.detailType == QContactBirthday::Type
            || detail.detailType == QContactAnniversary::Type) {
        // just interested in the date, not the whole date time
        return ContactsDatabase::dateString(qdt.toUTC());
    }

    return ContactsDatabase::dateTimeString(qdt.toUTC());
}

template<typename T1, typename T2>
static bool matchOnType(const T1 &filter, T2 type)
{
    return filter.detailType() == type;
}

template<typename T, typename F>
static bool filterOnField(const QContactDetailFilter &filter, F field)
{
    return (filter.detailType() == T::Type &&
            filter.detailField() == field);
}

template<typename F>
static bool validFilterField(F filter)
{
    return (filter.detailField() != -1);
}

template<typename F>
static int filterField(F filter) { return filter.detailField(); }

static QString convertFilterValueToString(const QContactDetailFilter &filter, const QString &defaultValue)
{
    // Some enum types are stored in textual form
    if (filter.detailType() == QContactOnlineAccount::Type) {
        if (filter.detailField() == QContactOnlineAccount::FieldProtocol) {
            return OnlineAccount::protocol(filter.value().toInt());
        } else if (filter.detailField() == QContactOnlineAccount::FieldSubTypes) {
            // TODO: what if the value is a list?
            return OnlineAccount::subTypeList(QList<int>() << filter.value().toInt()).first();
        }
    } else if (filter.detailType() == QContactPhoneNumber::Type) {
        if (filter.detailField() == QContactPhoneNumber::FieldSubTypes) {
            // TODO: what if the value is a list?
            return PhoneNumber::subTypeList(QList<int>() << filter.value().toInt()).first();
        }
    } else if (filter.detailType() == QContactAnniversary::Type) {
        if (filter.detailField() == QContactAnniversary::FieldSubType) {
            return Anniversary::subType(filter.value().toInt());
        }
    } else if (filter.detailType() == QContactUrl::Type) {
        if (filter.detailField() == QContactUrl::FieldSubType) {
            return Url::subType(filter.value().toInt());
        }
    } else if (filter.detailType() == QContactGender::Type) {
        if (filter.detailField() == QContactGender::FieldGender) {
            return Gender::gender(filter.value().toInt());
        }
    }

    return defaultValue;
}

static QString buildWhere(const QContactDetailFilter &filter, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    if (filter.matchFlags() & QContactFilter::MatchKeypadCollation) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with filter requiring keypad collation"));
        return QLatin1String("FAILED");
    }

    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!matchOnType(filter, detail.detailType))
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];

            if (validFilterField(filter) && (filterField(filter) != field.field))
                continue;

            if (!validFilterField(filter)            // "match if detail exists, don't care about field or value" filter
                    || !filter.value().isValid()     // "match if detail and field exists, don't care about value" filter
                    || (filterOnField<QContactSyncTarget>(filter, QContactSyncTarget::FieldSyncTarget) &&
                        filter.value().toString().isEmpty())) { // match all sync targets if empty sync target filter
                const QString comparison(QLatin1String("%1 IS NOT NULL"));
                return detail.where().arg(comparison.arg(field.column));
            }

            if (field.fieldType == OtherField) {
                if (filterOnField<QContactStatusFlags>(filter, QContactStatusFlags::FieldFlags)) {
                    static const quint64 flags[] = { QContactStatusFlags::HasPhoneNumber,
                                                     QContactStatusFlags::HasEmailAddress,
                                                     QContactStatusFlags::HasOnlineAccount,
                                                     QContactStatusFlags::IsOnline,
                                                     QContactStatusFlags::IsDeactivated,
                                                     QContactStatusFlags::IsIncidental };
                    static const char *flagColumns[] = { "hasPhoneNumber",
                                                         "hasEmailAddress",
                                                         "hasOnlineAccount",
                                                         "isOnline",
                                                         "isDeactivated",
                                                         "isIncidental" };

                    quint64 flagsValue = filter.value().value<quint64>();

                    QStringList clauses;
                    if (filter.matchFlags() == QContactFilter::MatchExactly) {
                        *globalPresenceRequired = true;
                        for (int i  = 0; i < lengthOf(flags); ++i) {
                            QString comparison;
                            if (flags[i] == QContactStatusFlags::IsOnline) {
                                // Use special case test to include transient presence state
                                comparison = QStringLiteral("COALESCE(temp.GlobalPresenceStates.isOnline, Contacts.isOnline) = %1");
                            } else {
                                comparison = QStringLiteral("%1 = %2").arg(flagColumns[i]);
                            }
                            clauses.append(comparison.arg((flagsValue & flags[i]) ? 1 : 0));
                        }
                    } else if (filter.matchFlags() == QContactFilter::MatchContains) {
                        for (int i  = 0; i < lengthOf(flags); ++i) {
                            if (flagsValue & flags[i]) {
                                if (flags[i] == QContactStatusFlags::IsOnline) {
                                    *globalPresenceRequired = true;
                                    clauses.append(QStringLiteral("COALESCE(temp.GlobalPresenceStates.isOnline, Contacts.isOnline) = 1"));
                                } else {
                                    clauses.append(QString::fromLatin1("%1 = 1").arg(flagColumns[i]));
                                }
                            }
                        }
                    } else {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unsupported flags matching contact status flags"));
                        continue;
                    }

                    if (!clauses.isEmpty()) {
                        return detail.where().arg(clauses.join(QString::fromLatin1(" AND ")));
                    }
                    continue;
                }
            }

            // TODO: We need case handling for StringListField, too
            bool dateField = field.fieldType == DateField;
            bool stringField = field.fieldType == StringField;
            bool phoneNumberMatch = filter.matchFlags() & QContactFilter::MatchPhoneNumber;
            bool useNormalizedNumber = false;
            int globValue = filter.matchFlags() & 7;

            // TODO: if MatchFixedString is specified but the field type is numeric, we need to
            // cast the column to text for comparison

            // We need to perform case-insensitive matching if MatchFixedString is specified (unless
            // CaseSensitive is also specified)
            bool caseInsensitive = stringField &&
                                   filter.matchFlags() & QContactFilter::MatchFixedString &&
                                   (filter.matchFlags() & QContactFilter::MatchCaseSensitive) == 0;

            QString clause(detail.where());
            QString comparison = QLatin1String("%1");
            QString bindValue;
            QString column;

            if (caseInsensitive) {
                column = caseInsensitiveColumnName(detail.table, field.column);
                if (!column.isEmpty()) {
                    // We don't need to use lower() on the values in this column
                } else {
                    comparison = QLatin1String("lower(%1)");
                }
            }

            QString stringValue = filter.value().toString();

            if (phoneNumberMatch) {
                // If the phone number match is on the number field of a phoneNumber detail, then
                // match on the normalized number rather than the unconstrained number (for simple matches)
                useNormalizedNumber = (filterOnField<QContactPhoneNumber>(filter, QContactPhoneNumber::FieldNumber) &&
                                       globValue != QContactFilter::MatchStartsWith &&
                                       globValue != QContactFilter::MatchContains &&
                                       globValue != QContactFilter::MatchEndsWith);

                if (useNormalizedNumber) {
                    // Normalize the input for comparison
                    bindValue = ContactsEngine::normalizedPhoneNumber(stringValue);
                    if (caseInsensitive) {
                        bindValue = bindValue.toLower();
                    }
                    column = QString::fromLatin1("normalizedNumber");
                } else {
                    // remove any non-digit characters from the column value when we do our comparison: +,-, ,#,(,) are removed.
                    comparison = QLatin1String("replace(replace(replace(replace(replace(replace(%1, '+', ''), '-', ''), '#', ''), '(', ''), ')', ''), ' ', '')");
                    QString tempValue = caseInsensitive ? stringValue.toLower() : stringValue;
                    for (int i = 0; i < tempValue.size(); ++i) {
                        QChar current = tempValue.at(i).toLower();
                        if (current.isDigit()) {
                            bindValue.append(current);
                        }
                    }
                }
            } else {
                const QVariant &v(filter.value());
                if (dateField) {
                    bindValue = dateString(detail, v.toDateTime());

                    if (filterOnField<QContactTimestamp>(filter, QContactTimestamp::FieldModificationTimestamp)) {
                        // Special case: we need to include the transient data timestamp in our comparison
                        column = QStringLiteral("COALESCE(temp.Timestamps.modified, Contacts.modified)");
                        *transientModifiedRequired = true;
                    }
                } else if (!stringField && (v.type() == QVariant::Bool)) {
                    // Convert to "1"/"0" rather than "true"/"false"
                    bindValue = QString::number(v.toBool() ? 1 : 0);
                } else {
                    stringValue = convertFilterValueToString(filter, stringValue);
                    bindValue = caseInsensitive ? stringValue.toLower() : stringValue;

                    if (filterOnField<QContactGlobalPresence>(filter, QContactGlobalPresence::FieldPresenceState)) {
                        // Special case: we need to include the transient data state in our comparison
                        clause = QLatin1String("Contacts.contactId IN ("
                                                   "SELECT GlobalPresences.contactId FROM GlobalPresences "
                                                   "LEFT JOIN temp.GlobalPresenceStates ON temp.GlobalPresenceStates.contactId = GlobalPresences.contactId "
                                                   "WHERE %1)");
                        column = QStringLiteral("COALESCE(temp.GlobalPresenceStates.presenceState, GlobalPresences.presenceState)");
                        *globalPresenceRequired = true;
                    }
                }
            }

            if (stringField) {
                if (globValue == QContactFilter::MatchStartsWith) {
                    bindValue = bindValue + QLatin1String("*");
                    comparison += QLatin1String(" GLOB ?");
                    bindings->append(bindValue);
                } else if (globValue == QContactFilter::MatchContains) {
                    bindValue = QLatin1String("*") + bindValue + QLatin1String("*");
                    comparison += QLatin1String(" GLOB ?");
                    bindings->append(bindValue);
                } else if (globValue == QContactFilter::MatchEndsWith) {
                    bindValue = QLatin1String("*") + bindValue;
                    comparison += QLatin1String(" GLOB ?");
                    bindings->append(bindValue);
                } else {
                    if (bindValue.isEmpty()) {
                        // An empty string test should match a NULL column also (no way to specify isNull from qtcontacts)
                        comparison = QString::fromLatin1("COALESCE(%1,'') = ''").arg(comparison);
                    } else {
                        comparison += QLatin1String(" = ?");
                        bindings->append(bindValue);
                    }
                }
            } else {
                if (phoneNumberMatch && !useNormalizedNumber) {
                    bindValue = QLatin1String("*") + bindValue;
                    comparison += QLatin1String(" GLOB ?");
                    bindings->append(bindValue);
                } else {
                    comparison += QLatin1String(" = ?");
                    bindings->append(bindValue);
                }
            }

            return clause.arg(comparison.arg(column.isEmpty() ? field.column : column));
        }
    }

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown DetailFilter detail: %1").arg(filter.detailType()));
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactDetailRangeFilter &filter, QVariantList *bindings, bool *failed)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!matchOnType(filter, detail.detailType))
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];

            if (validFilterField(filter) && (filterField(filter) != field.field))
                continue;

            if (!validFilterField(filter) || (!filter.minValue().isValid() && !filter.maxValue().isValid())) {
                // "match if detail exists, don't care about field or value" filter
                const QString comparison(QLatin1String("%1 IS NOT NULL"));
                return detail.where().arg(comparison.arg(field.column));
            }

            QString comparison;
            bool dateField = field.fieldType == DateField;
            bool stringField = field.fieldType == StringField;
            bool caseInsensitive = stringField &&
                                   filter.matchFlags() & QContactFilter::MatchFixedString &&
                                   (filter.matchFlags() & QContactFilter::MatchCaseSensitive) == 0;

            bool needsAnd = false;
            if (filter.minValue().isValid()) {
                if (dateField) {
                    bindings->append(dateString(detail, filter.minValue().toDateTime()));
                } else {
                    bindings->append(filter.minValue());
                }
                if (caseInsensitive) {
                    comparison = (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeLower)
                            ? QString(QLatin1String("%1 > lower(?)"))
                            : QString(QLatin1String("%1 >= lower(?)"));
                } else {
                    comparison = (filter.rangeFlags() & QContactDetailRangeFilter::ExcludeLower)
                            ? QString(QLatin1String("%1 > ?"))
                            : QString(QLatin1String("%1 >= ?"));
                }
                needsAnd = true;
            }

            if (filter.maxValue().isValid()) {
                if (needsAnd)
                    comparison += QLatin1String(" AND ");
                if (dateField) {
                    bindings->append(dateString(detail, filter.maxValue().toDateTime()));
                } else {
                    bindings->append(filter.maxValue());
                }
                if (caseInsensitive) {
                    comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                            ? QString(QLatin1String("%1 <= lower(?)"))
                            : QString(QLatin1String("%1 < lower(?)"));
                } else {
                    comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                            ? QString(QLatin1String("%1 <= ?"))
                            : QString(QLatin1String("%1 < ?"));
                }
                
            }

            QString comparisonArg = field.column;
            if (caseInsensitive) {
                comparisonArg = caseInsensitiveColumnName(detail.table, field.column);
                if (!comparisonArg.isEmpty()) {
                    // We don't need to use lower() on the values in this column
                } else {
                    comparisonArg = QString::fromLatin1("lower(%1)").arg(field.column);
                }
            }
            return detail.where().arg(comparison.arg(comparisonArg));
        }
    }

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown DetailRangeFilter detail: %1").arg(filter.detailType()));
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactIdFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings, bool *failed)
{
    const QList<QContactId> &filterIds(filter.ids());
    if (filterIds.isEmpty()) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with empty contact ID list"));
        return QLatin1String("FALSE");
    }

    QList<quint32> dbIds;
    dbIds.reserve(filterIds.count());
    bindings->reserve(filterIds.count());

    foreach (const QContactId &id, filterIds) {
        dbIds.append(ContactId::databaseId(id));
    }

    // We don't want to exceed the maximum bound variables limit; if there are too
    // many IDs in the list, create a temporary table to look them up from
    const int maxInlineIdsCount = 800;
    if (filterIds.count() > maxInlineIdsCount) {
        QVariantList varIds;
        foreach (const QContactId &id, filterIds) {
            varIds.append(QVariant(ContactId::databaseId(id)));
        }

        QString transientTable;
        if (!db.createTransientContactIdsTable(table, varIds, &transientTable)) {
            *failed = true;
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere due to transient table failure"));
            return QLatin1String("FALSE");
        }

        return QString::fromLatin1("Contacts.contactId IN (SELECT contactId FROM %1)").arg(transientTable);
    }

    QString statement = QLatin1String("Contacts.contactId IN (?");
    bindings->append(dbIds.first());

    for (int i = 1; i < dbIds.count(); ++i) {
        statement += QLatin1String(",?");
        bindings->append(dbIds.at(i));
    }
    return statement + QLatin1String(")");
}

static QString buildWhere(const QContactRelationshipFilter &filter, QVariantList *bindings, bool *failed)
{
    QContactId rci = filter.relatedContact().id();

    QContactRelationship::Role rcr = filter.relatedContactRole();
    QString rt = filter.relationshipType();

    quint32 dbId = ContactId::databaseId(rci);

    if (!rci.managerUri().isEmpty() && !rci.managerUri().startsWith(QString::fromLatin1("qtcontacts:org.nemomobile.contacts.sqlite:"))) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with invalid manager URI: %1").arg(rci.managerUri()));
        return QLatin1String("FALSE");
    }

    bool needsId = dbId != 0;
    bool needsType = !rt.isEmpty();
    QString statement = QLatin1String("Contacts.contactId IN (\n");
    if (!needsId && !needsType) {
        // return the id of every contact who is in a relationship
        if (rcr == QContactRelationship::First) { // where the other contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships)");
        } else if (rcr == QContactRelationship::Second) { // where the other contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships)");
        } else { // where the other contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships UNION SELECT DISTINCT secondId FROM Relationships)");
        }
    } else if (!needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type
        if (rcr == QContactRelationship::First) { // where the other contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE type = ?)");
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the other contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE type = ?)");
            bindings->append(rt);
        } else { // where the other contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE type = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE type = ?)");
            bindings->append(rt);
            bindings->append(rt);
        }
    } else if (needsId && !needsType) {
        // return the id of every contact who is in a relationship with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?)");
            bindings->append(dbId);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ?)");
            bindings->append(dbId);
        } else { // where the specified contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?)");
            bindings->append(dbId);
            bindings->append(dbId);
        }
    } else if (needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?)");
            bindings->append(dbId);
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ?)");
            bindings->append(dbId);
            bindings->append(rt);
        } else { // where the specified contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?)");
            bindings->append(dbId);
            bindings->append(rt);
            bindings->append(dbId);
            bindings->append(rt);
        }
    }

    return statement;
}

static QString buildWhere(const QContactChangeLogFilter &filter, QVariantList *bindings, bool *failed, bool *transientModifiedRequired)
{
    static const QString statement(QLatin1String("%1 >= ?"));
    bindings->append(ContactsDatabase::dateTimeString(filter.since().toUTC()));
    switch (filter.eventType()) {
        case QContactChangeLogFilter::EventAdded:
            return statement.arg(QLatin1String("Contacts.created"));
        case QContactChangeLogFilter::EventChanged:
            *transientModifiedRequired = true;
            return statement.arg(QLatin1String("COALESCE(temp.Timestamps.modified, Contacts.modified)"));
        default: break;
    }

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with changelog filter on removed timestamps"));
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired);

static QString buildWhere(const QContactUnionFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
        if (!*failed && !fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    return QString::fromLatin1("( %1 )").arg(fragments.join(QLatin1String(" OR ")));
}

static QString buildWhere(const QContactIntersectionFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
        if (filter.type() != QContactFilter::DefaultFilter && !*failed) {
            // default filter gets special (permissive) treatment by the intersection filter.
            fragments.append(fragment.isEmpty() ? QLatin1String("NULL") : fragment);
        }
    }

    return fragments.join(QLatin1String(" AND "));
}

static QString buildWhere(const QContactFilter &filter, ContactsDatabase &db, const QString &table, QVariantList *bindings,
                          bool *failed, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    Q_ASSERT(failed);
    Q_ASSERT(globalPresenceRequired);
    Q_ASSERT(transientModifiedRequired);

    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
        return QString();
    case QContactFilter::ContactDetailFilter:
        return buildWhere(static_cast<const QContactDetailFilter &>(filter), bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::ContactDetailRangeFilter:
        return buildWhere(static_cast<const QContactDetailRangeFilter &>(filter), bindings, failed);
    case QContactFilter::ChangeLogFilter:
        return buildWhere(static_cast<const QContactChangeLogFilter &>(filter), bindings, failed, transientModifiedRequired);
    case QContactFilter::RelationshipFilter:
        return buildWhere(static_cast<const QContactRelationshipFilter &>(filter), bindings, failed);
    case QContactFilter::IntersectionFilter:
        return buildWhere(static_cast<const QContactIntersectionFilter &>(filter), db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::UnionFilter:
        return buildWhere(static_cast<const QContactUnionFilter &>(filter), db, table, bindings, failed, transientModifiedRequired, globalPresenceRequired);
    case QContactFilter::IdFilter:
        return buildWhere(static_cast<const QContactIdFilter &>(filter), db, table, bindings, failed);
    default:
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown filter type: %1").arg(filter.type()));
        return QLatin1String("FALSE");
    }
}

static int sortField(const QContactSortOrder &sort) { return sort.detailField(); }

static QString buildOrderBy(const QContactSortOrder &order, QStringList *joins, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    Q_ASSERT(joins);
    Q_ASSERT(transientModifiedRequired);
    Q_ASSERT(globalPresenceRequired);

    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!matchOnType(order, detail.detailType))
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];
            if (sortField(order) != field.field)
                continue;

            QString sortExpression(QStringLiteral("%1.%2").arg(detail.join ? detail.table : QStringLiteral("Contacts")).arg(field.column));
            bool sortBlanks = true;
            bool collate = true;

            // Special case for accessing transient data
            if (detail.detailType == detailIdentifier<QContactGlobalPresence>() &&
                field.field == QContactGlobalPresence::FieldPresenceState) {
                // We need to coalesce the transient values with the table values
                *globalPresenceRequired = true;

                // Look at the temporary state value if present, otherwise use the normal value
                sortExpression = QStringLiteral("COALESCE(temp.GlobalPresenceStates.presenceState, GlobalPresences.presenceState)");
                sortBlanks = false;
                collate = false;

#ifdef SORT_PRESENCE_BY_AVAILABILITY
                // The order we want is Available(1),Away(4),ExtendedAway(5),Busy(3),Hidden(2),Offline(6),Unknown(0)
                sortExpression = QStringLiteral("CASE %1 WHEN 1 THEN 0 "
                                                        "WHEN 4 THEN 1 "
                                                        "WHEN 5 THEN 2 "
                                                        "WHEN 3 THEN 3 "
                                                        "WHEN 2 THEN 4 "
                                                        "WHEN 6 THEN 5 "
                                                               "ELSE 6 END").arg(sortExpression);
#endif
            } else if (detail.detailType == detailIdentifier<QContactTimestamp>() &&
                       field.field == QContactTimestamp::FieldModificationTimestamp) {
                *transientModifiedRequired = true;

                // Look at the temporary modified timestamp if present, otherwise use the normal value
                sortExpression = QStringLiteral("COALESCE(temp.Timestamps.modified, modified)");
                sortBlanks = false;
                collate = false;
            }

            QString result;

            if (sortBlanks) {
                QString blanksLocation = (order.blankPolicy() == QContactSortOrder::BlanksLast)
                        ? QLatin1String("CASE WHEN %1 IS NULL OR %1 = '' THEN 1 ELSE 0 END, ")
                        : QLatin1String("CASE WHEN %1 IS NULL OR %1 = '' THEN 0 ELSE 1 END, ");
                result = blanksLocation.arg(sortExpression);
            }

            result.append(sortExpression);

            if (collate) {
                result.append((order.caseSensitivity() == Qt::CaseSensitive) ? QLatin1String(" COLLATE RTRIM") : QLatin1String(" COLLATE NOCASE"));
            }

            result.append((order.direction() == Qt::AscendingOrder) ? QLatin1String(" ASC") : QLatin1String(" DESC"));

            if (detail.join) {
                QString join = QString(QLatin1String(
                        "LEFT JOIN %1 ON Contacts.contactId = %1.contactId"))
                        .arg(QLatin1String(detail.table));

                if (!joins->contains(join))
                    joins->append(join);

                return result;
            } else if (!detail.table) {
                return result;
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("UNSUPPORTED SORTING: no join and not primary table for ORDER BY in query with: %1, %2")
                           .arg(order.detailType()).arg(order.detailField()));
            }
        }
    }

    return QString();
}

static QString buildOrderBy(const QList<QContactSortOrder> &order, QString *join, bool *transientModifiedRequired, bool *globalPresenceRequired)
{
    Q_ASSERT(join);
    Q_ASSERT(transientModifiedRequired);
    Q_ASSERT(globalPresenceRequired);

    if (order.isEmpty())
        return QString();

    QStringList joins;
    QStringList fragments;
    foreach (const QContactSortOrder &sort, order) {
        const QString fragment = buildOrderBy(sort, &joins, transientModifiedRequired, globalPresenceRequired);
        if (!fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    *join = joins.join(QLatin1String(" "));

    fragments.append(QLatin1String("displayLabel"));
    return fragments.join(QLatin1String(", "));
}

static void debugFilterExpansion(const QString &description, const QString &query, const QVariantList &bindings)
{
    static const bool debugFilters = !qgetenv("QTCONTACTS_SQLITE_DEBUG_FILTERS").isEmpty();

    if (debugFilters) {
        qDebug() << description << ContactsDatabase::expandQuery(query, bindings);
    }
}

ContactReader::ContactReader(ContactsDatabase &database)
    : m_database(database)
{
}

ContactReader::~ContactReader()
{
}

struct Table
{
    QSqlQuery *query;
    QContactDetail::DetailType detailType;
    ReadDetail read;
    quint32 currentId;
};

namespace {

// The selfId is fixed - DB ID 1 is the 'self' local contact, and DB ID 2 is the aggregate
const quint32 selfId(2);

bool includesSelfId(const QContactFilter &filter);

// Returns true if this filter includes the self contact by ID
bool includesSelfId(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesSelfId(filter)) {
            return true;
        }
    }
    return false;
}
bool includesSelfId(const QContactIntersectionFilter &filter)
{
    return includesSelfId(filter.filters());
}
bool includesSelfId(const QContactUnionFilter &filter)
{
    return includesSelfId(filter.filters());
}
bool includesSelfId(const QContactIdFilter &filter)
{
    foreach (const QContactId &id, filter.ids()) {
        if (ContactId::databaseId(id) == selfId)
            return true;
    }
    return false;
}
bool includesSelfId(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesSelfId(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesSelfId(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::IdFilter:
        return includesSelfId(static_cast<const QContactIdFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesSelfId with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesSyncTarget(const QContactFilter &filter);

// Returns true if this filter includes a filter for specific syncTarget
bool includesSyncTarget(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesSyncTarget(filter)) {
            return true;
        }
    }
    return false;
}
bool includesSyncTarget(const QContactIntersectionFilter &filter)
{
    return includesSyncTarget(filter.filters());
}
bool includesSyncTarget(const QContactUnionFilter &filter)
{
    return includesSyncTarget(filter.filters());
}
bool includesSyncTarget(const QContactDetailFilter &filter)
{
    return filter.detailType() == QContactSyncTarget::Type;
}
bool includesSyncTarget(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
    case QContactFilter::IdFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesSyncTarget(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesSyncTarget(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::ContactDetailFilter:
        return includesSyncTarget(static_cast<const QContactDetailFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesSyncTarget with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesDeactivated(const QContactFilter &filter);

// Returns true if this filter includes deactivated contacts
bool includesDeactivated(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesDeactivated(filter)) {
            return true;
        }
    }
    return false;
}
bool includesDeactivated(const QContactIntersectionFilter &filter)
{
    return includesDeactivated(filter.filters());
}
bool includesDeactivated(const QContactUnionFilter &filter)
{
    return includesDeactivated(filter.filters());
}
bool includesDeactivated(const QContactDetailFilter &filter)
{
    if (filterOnField<QContactStatusFlags>(filter, QContactStatusFlags::FieldFlags)) {
        quint64 flagsValue = filter.value().value<quint64>();
        if (flagsValue & QContactStatusFlags::IsDeactivated) {
            return true;
        }
    }
    return false;
}
bool includesDeactivated(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::IdFilter:
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesDeactivated(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesDeactivated(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::ContactDetailFilter:
        return includesDeactivated(static_cast<const QContactDetailFilter &>(filter));

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesDeactivated with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

bool includesIdFilter(const QContactFilter &filter);

// Returns true if this filter includes a filter for specific IDs
bool includesIdFilter(const QList<QContactFilter> &filters)
{
    foreach (const QContactFilter &filter, filters) {
        if (includesIdFilter(filter)) {
            return true;
        }
    }
    return false;
}
bool includesIdFilter(const QContactIntersectionFilter &filter)
{
    return includesIdFilter(filter.filters());
}
bool includesIdFilter(const QContactUnionFilter &filter)
{
    return includesIdFilter(filter.filters());
}
bool includesIdFilter(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::ChangeLogFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesIdFilter(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesIdFilter(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::IdFilter:
        return true;

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesIdFilter with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

static bool deletedContactFilter(const QContactFilter &filter)
{
    const QContactFilter::FilterType filterType(filter.type());

    // The only queries we suport regarding deleted contacts are for the IDs, possibly
    // intersected with a syncTarget detail filter
    if (filterType == QContactFilter::ChangeLogFilter) {
        const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(filter));
        return changeLogFilter.eventType() == QContactChangeLogFilter::EventRemoved;
    } else if (filterType == QContactFilter::IntersectionFilter) {
        const QContactIntersectionFilter &intersectionFilter(static_cast<const QContactIntersectionFilter &>(filter));
        const QList<QContactFilter> filters(intersectionFilter.filters());
        if (filters.count() <= 2) {
            foreach (const QContactFilter &partialFilter, filters) {
                if (partialFilter.type() == QContactFilter::ChangeLogFilter) {
                    const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(partialFilter));
                    if (changeLogFilter.eventType() == QContactChangeLogFilter::EventRemoved) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

QString expandWhere(const QString &where, const QContactFilter &filter, const bool aggregating)
{
    QStringList constraints;

    // remove the self contact, unless specifically included
    if (!includesSelfId(filter)) {
        constraints.append("Contacts.contactId > 2 ");
    }

    // if the filter does not specify contacts by ID
    if (!includesIdFilter(filter)) {
        if (aggregating) {
            // exclude non-aggregates, unless the filter specifies syncTarget
            if (!includesSyncTarget(filter)) {
                constraints.append("Contacts.syncTarget = 'aggregate' ");
            }
        }

        // exclude deactivated unless they're explicitly included
        if (!includesDeactivated(filter)) {
            constraints.append("Contacts.isDeactivated = 0 ");
        }
    }

    // some (union) filters can add spurious braces around empty expressions
    bool emptyFilter = false;
    {
        QString strippedWhere = where;
        strippedWhere.remove(QChar('('));
        strippedWhere.remove(QChar(')'));
        strippedWhere.remove(QChar(' '));
        emptyFilter = strippedWhere.isEmpty();
    }

    if (emptyFilter && constraints.isEmpty())
        return QString();

    QString whereClause(QString::fromLatin1("WHERE "));
    if (!constraints.isEmpty()) {
        whereClause += constraints.join(QLatin1String("AND "));
        if (!emptyFilter) {
            whereClause += QLatin1String("AND ");
        }
    }
    if (!emptyFilter) {
        whereClause += where;
    }

    return whereClause;
}

}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order,
        const QContactFetchHint &fetchHint)
{
    QMutexLocker locker(m_database.accessMutex());

    m_database.clearTemporaryContactIdsTable(table);

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(order, &join, &transientModifiedRequired, &globalPresenceRequired);

    bool whereFailed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, m_database, table, &bindings, &whereFailed, &transientModifiedRequired, &globalPresenceRequired);
    if (whereFailed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter, m_database.aggregating());

    if (transientModifiedRequired || globalPresenceRequired) {
        // Provide the temporary transient state information to filter/sort on
        if (!m_database.populateTemporaryTransientState(transientModifiedRequired, globalPresenceRequired)) {
            return QContactManager::UnspecifiedError;
        }

        if (transientModifiedRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.Timestamps ON Contacts.contactId = temp.Timestamps.contactId"));
        }
        if (globalPresenceRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.GlobalPresenceStates ON Contacts.contactId = temp.GlobalPresenceStates.contactId"));
        }
    }

    QContactManager::Error error = QContactManager::NoError;
    if (!m_database.createTemporaryContactIdsTable(table, join, where, orderBy, bindings)) {
        error = QContactManager::UnspecifiedError;
    } else {
        error = queryContacts(table, contacts, fetchHint);
    }

    return error;
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<QContactId> &contactIds,
        const QContactFetchHint &fetchHint)
{
    QList<quint32> databaseIds;
    databaseIds.reserve(contactIds.size());

    foreach (const QContactId &id, contactIds) {
        databaseIds.append(ContactId::databaseId(id));
    }

    return readContacts(table, contacts, databaseIds, fetchHint);
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<quint32> &databaseIds,
        const QContactFetchHint &fetchHint,
        bool relaxConstraints)
{
    QMutexLocker locker(m_database.accessMutex());

    QVariantList boundIds;
    boundIds.reserve(databaseIds.size());
    foreach (quint32 id, databaseIds) {
        boundIds.append(id);
    }

    contacts->reserve(databaseIds.size());

    m_database.clearTemporaryContactIdsTable(table);

    QContactManager::Error error = QContactManager::NoError;
    if (!m_database.createTemporaryContactIdsTable(table, boundIds)) {
        error = QContactManager::UnspecifiedError;
    } else {
        error = queryContacts(table, contacts, fetchHint, relaxConstraints);
    }

    // the ordering of the queried contacts is identical to
    // the ordering of the input contact ids list.
    int contactIdsSize = databaseIds.size();
    int contactsSize = contacts->size();
    if (contactIdsSize != contactsSize) {
        for (int i = 0; i < contactIdsSize; ++i) {
            if (i >= contactsSize || ContactId::databaseId((*contacts)[i].id()) != databaseIds[i]) {
                // the id list contained a contact id which doesn't exist
                contacts->insert(i, QContact());
                contactsSize++;
                error = QContactManager::DoesNotExistError;
            }
        }
    }

    return error;
}

QContactManager::Error ContactReader::queryContacts(
        const QString &tableName, QList<QContact> *contacts, const QContactFetchHint &fetchHint, bool relaxConstraints)
{
    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.exec(QString(QLatin1String(
            "\n SELECT Contacts.*"
            "\n FROM temp.%1 INNER JOIN Contacts ON temp.%1.contactId = Contacts.contactId"
            "\n ORDER BY temp.%1.rowId ASC;")).arg(tableName))) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query from %1: %2").arg(tableName).arg(query.lastError().text()));
        return QContactManager::UnspecifiedError;
    }

    QList<QSqlQuery> activeQueries;
    QContactManager::Error err = queryContacts(tableName, contacts, fetchHint, relaxConstraints, query, &activeQueries);

    // Ensure all active queries are completed
    query.finish();
    QList<QSqlQuery>::iterator it = activeQueries.begin(), end = activeQueries.end();
    for ( ; it != end; ++it) {
        (*it).finish();
    }

    return err;
}

QContactManager::Error ContactReader::queryContacts(
        const QString &tableName, QList<QContact> *contacts, const QContactFetchHint &fetchHint, bool relaxConstraints, QSqlQuery &query, QList<QSqlQuery> *activeQueries)
{
    Q_ASSERT(activeQueries);

    const QString tableTemplate = QString(QLatin1String(
            "\n SELECT"
            "\n  Details.detailUri,"
            "\n  Details.linkedDetailUris,"
            "\n  Details.contexts,"
            "\n  Details.accessConstraints,"
            "\n  Details.provenance,"
            "\n  COALESCE(Details.modifiable, 0),"
            "\n  COALESCE(Details.nonexportable, 0),"
            "\n  %2.*"
            "\n FROM temp.%1"
            "\n  INNER JOIN %2 ON temp.%1.contactId = %2.contactId"
            "\n  LEFT JOIN Details ON %2.detailId = Details.detailId AND Details.detail = :detail"
            "\n ORDER BY temp.%1.rowId ASC;")).arg(tableName);

    // This is the zero-based offset of the contact ID value in the table query above:
    const int idValueOffset = 8;

    const ContactWriter::DetailList &definitionMask = fetchHint.detailTypesHint();

    QMap<QString, QSqlQuery> &cachedQueries(m_cachedDetailTableQueries[tableName]);

    QList<Table> tables;
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!detail.read)
            continue;

        if (definitionMask.isEmpty() || definitionMask.contains(detail.detailType)) {
            // we need to query this particular detail table
            // use cached prepared queries if available, else prepare and cache query.
            QMap<QString, QSqlQuery>::iterator qit = cachedQueries.find(detail.table);
            if (qit == cachedQueries.end()) {
                // Not yet in the cache
                const QString tableQueryStatement(tableTemplate.arg(QLatin1String(detail.table)));
                QSqlQuery detailTableQuery(m_database);
                detailTableQuery.setForwardOnly(true);
                if (!detailTableQuery.prepare(tableQueryStatement)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare table %1:\n%2\n%3")
                            .arg(detail.table)
                            .arg(tableQueryStatement)
                            .arg(detailTableQuery.lastError().text()));
                    return QContactManager::UnspecifiedError;
                } else {
                    qit = cachedQueries.insert(detail.table, detailTableQuery);
                }
            }

            // Make a copy of the prepared query
            activeQueries->append(*qit);
            QSqlQuery &tableQuery(activeQueries->last());

            tableQuery.bindValue(0, QString::fromLatin1(detail.detailName));
            if (!tableQuery.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query table %1:\n%2")
                        .arg(detail.table)
                        .arg(tableQuery.lastError().text()));
                return QContactManager::UnspecifiedError;
            } else if (tableQuery.next()) {
                quint32 currentId = tableQuery.value(idValueOffset).toUInt();

                Table table = { &tableQuery, detail.detailType, detail.read, currentId };
                tables.append(table);
            }
        }
    }

    QContactFetchHint::OptimizationHints optimizationHints(fetchHint.optimizationHints());

    const int maximumCount = fetchHint.maxCountHint();
    const int batchSize = (maximumCount > 0) ? maximumCount : ReportBatchSize;

    do {
        int contactCount = contacts->count();
        QList<bool> syncableContact;
        QStringList iidList;
        QMap<quint32, int> contactIdIndex;
        QMap<QContactDetail::DetailType, QList<quint32> > transientDetailContacts;

        for (int i = 0; i < batchSize && query.next(); ++i) {
            quint32 dbId = query.value(0).toUInt();
            QContact contact;

            QContactId id(ContactId::contactId(ContactId::apiId(dbId)));
            contact.setId(id);

            iidList.append(QString::number(dbId));
            contactIdIndex.insert(dbId, contactCount + i);

            QString persistedDL = query.value(1).toString();
            if (!persistedDL.isEmpty())
                ContactsEngine::setContactDisplayLabel(&contact, persistedDL);

            QContactName name;
            setValue(&name, QContactName::FieldFirstName  , query.value(2));
            // ignore lowerFirstName
            setValue(&name, QContactName::FieldLastName   , query.value(4));
            // ignore lowerLastName
            setValue(&name, QContactName::FieldMiddleName , query.value(6));
            setValue(&name, QContactName::FieldPrefix     , query.value(7));
            setValue(&name, QContactName::FieldSuffix     , query.value(8));
            setValue(&name, QContactName__FieldCustomLabel, query.value(9));
            if (!name.isEmpty())
                contact.saveDetail(&name);

            const QString syncTarget(query.value(10).toString());

            QContactSyncTarget starget;
            setValue(&starget, QContactSyncTarget::FieldSyncTarget, syncTarget);
            if (!starget.isEmpty())
                contact.saveDetail(&starget);

            QContactTimestamp timestamp;
            setValue(&timestamp, QContactTimestamp::FieldCreationTimestamp    , ContactsDatabase::fromDateTimeString(query.value(11).toString()));
            setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, ContactsDatabase::fromDateTimeString(query.value(12).toString()));

            QContactGender gender;
            // Gender is an enum in qtpim
            QString genderText = query.value(13).toString();
            if (genderText.startsWith(QChar::fromLatin1('f'), Qt::CaseInsensitive)) {
                gender.setGender(QContactGender::GenderFemale);
            } else if (genderText.startsWith(QChar::fromLatin1('m'), Qt::CaseInsensitive)) {
                gender.setGender(QContactGender::GenderMale);
            } else {
                gender.setGender(QContactGender::GenderUnspecified);
            }
            if (!gender.isEmpty())
                contact.saveDetail(&gender);

            QContactFavorite favorite;
            setValue(&favorite, QContactFavorite::FieldFavorite, query.value(14).toBool());
            if (!favorite.isEmpty())
                contact.saveDetail(&favorite);

            QContactStatusFlags flags;
            flags.setFlag(QContactStatusFlags::HasPhoneNumber, query.value(15).toBool());
            flags.setFlag(QContactStatusFlags::HasEmailAddress, query.value(16).toBool());
            flags.setFlag(QContactStatusFlags::HasOnlineAccount, query.value(17).toBool());
            flags.setFlag(QContactStatusFlags::IsOnline, query.value(18).toBool());
            flags.setFlag(QContactStatusFlags::IsDeactivated, query.value(19).toBool());
            flags.setFlag(QContactStatusFlags::IsIncidental, query.value(20).toBool());

            if (flags.testFlag(QContactStatusFlags::IsDeactivated)) {
                QContactDeactivated deactivated;
                contact.saveDetail(&deactivated);
            }
            if (flags.testFlag(QContactStatusFlags::IsIncidental)) {
                QContactIncidental incidental;
                contact.saveDetail(&incidental);
            }

            int contactType = query.value(21).toInt();
            QContactType typeDetail = contact.detail<QContactType>();
            typeDetail.setType(static_cast<QContactType::TypeValues>(contactType));
            contact.saveDetail(&typeDetail);

            bool syncable = !syncTarget.isEmpty() &&
                            (syncTarget != aggregateSyncTarget) &&
                            (syncTarget != localSyncTarget) &&
                            (syncTarget != wasLocalSyncTarget);
            syncableContact.append(syncable);

            // Find any transient details for this contact
            if (m_database.hasTransientDetails(dbId)) {
                QSet<QContactDetail::DetailType> transientTypes;

                const QPair<QDateTime, QList<QContactDetail> > transientDetails(m_database.transientDetails(dbId));
                if (!transientDetails.first.isNull()) {
                    // Update the contact timestamp to that of the transient details
                    setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, transientDetails.first);

                    QList<QContactDetail>::const_iterator it = transientDetails.second.constBegin(), end = transientDetails.second.constEnd();
                    for ( ; it != end; ++it) {
                        // Copy the transient detail into the contact
                        const QContactDetail &transient(*it);

                        const QContactDetail::DetailType transientType(transient.type());

                        if (transientType == QContactGlobalPresence::Type) {
                            // If global presence is in the transient details, the IsOnline status flag is out of date
                            const int presenceState = transient.value<int>(QContactGlobalPresence::FieldPresenceState);
                            const bool isOnline(presenceState >= QContactPresence::PresenceAvailable &&
                                                presenceState <= QContactPresence::PresenceExtendedAway);
                            flags.setFlag(QContactStatusFlags::IsOnline, isOnline);
                        }

                        // Ignore details that aren't in the requested types
                        if (!definitionMask.isEmpty() && !definitionMask.contains(transientType)) {
                            continue;
                        }

                        QContactDetail detail(transient.type());
                        if (!relaxConstraints) {
                            QContactManagerEngine::setDetailAccessConstraints(&detail, transient.accessConstraints());
                        }

                        const QMap<int, QVariant> values(transient.values());
                        QMap<int, QVariant>::const_iterator vit = values.constBegin(), vend = values.constEnd();
                        for ( ; vit != vend; ++vit) {
                            bool append(true);

                            if (vit.key() == QContactDetail__FieldModifiable) {
                                append = syncable;
                            }

                            if (append) {
                                detail.setValue(vit.key(), vit.value());
                            }
                        }

                        contact.saveDetail(&detail);

                        transientTypes.insert(transientType);
                    }
                }

                // Mark this contact as not requiring table data for these detailtypes
                foreach (QContactDetail::DetailType type, transientTypes) {
                    QList<quint32> &idList(transientDetailContacts[type]);
                    idList.append(dbId);
                }
            }

            // Add the updated status flags
            QContactManagerEngine::setDetailAccessConstraints(&flags, QContactDetail::ReadOnly | QContactDetail::Irremovable);
            contact.saveDetail(&flags);

            // Add the timestamp info
            if (!timestamp.isEmpty())
                contact.saveDetail(&timestamp);

            contacts->append(contact);
        }

        for (int j = 0; j < tables.count(); ++j) {
            Table &table = tables[j];
            const QList<quint32> &transientContactIds(transientDetailContacts[table.detailType]);

            QList<bool>::iterator sit = syncableContact.begin();
            QList<QContact>::iterator it = contacts->begin() + contactCount;
            for (QList<QContact>::iterator end = contacts->end(); it != end; ++it, ++sit) {
                QContact &contact(*it);
                quint32 contactId = ContactId::databaseId(contact.id());

                if (table.query->isValid() && (table.currentId == contactId)) {
                    // If the transient store contains details of this type for this contact, they take precedence over the
                    // details stored in the tables
                    const bool contactHasTransient(transientContactIds.contains(contactId));

                    table.read(contactId, table.currentId, &contact, table.query, *sit, relaxConstraints, !contactHasTransient);
                }
            }
        }

        if ((optimizationHints & QContactFetchHint::NoRelationships) == 0) {
            // Find the relationships for the contacts in this batch
            QMap<quint32, QList<QContactRelationship> > contactRelationships;

            QString queryStatement(QString::fromLatin1(
                "SELECT type,firstId,secondId "
                "FROM Relationships "
                "WHERE (firstId IN (%1) OR secondId IN (%1))").arg(iidList.join(QChar::fromLatin1(','))));

            QSqlQuery relationshipQuery(m_database);
            relationshipQuery.setForwardOnly(true);
            if (!relationshipQuery.prepare(queryStatement)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query for contact relationships:\n%1\nQuery:\n%2")
                        .arg(relationshipQuery.lastError().text())
                        .arg(queryStatement));
                return QContactManager::UnspecifiedError;
            }
            if (!relationshipQuery.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query contact relationships\n%1\nQuery:\n%2")
                        .arg(relationshipQuery.lastError().text())
                        .arg(queryStatement));
                return QContactManager::UnspecifiedError;
            }

            while (relationshipQuery.next()) {
                QString type = relationshipQuery.value(0).toString();
                quint32 firstId = relationshipQuery.value(1).toUInt();
                quint32 secondId = relationshipQuery.value(2).toUInt();

                QContactRelationship rel(makeRelationship(type, firstId, secondId));

                QMap<quint32, int>::iterator iit = contactIdIndex.find(firstId);
                if (iit != contactIdIndex.end()) {
                    contactRelationships[firstId].append(rel);
                }
                iit = contactIdIndex.find(secondId);
                if (iit != contactIdIndex.end()) {
                    contactRelationships[secondId].append(rel);
                }
            }

            relationshipQuery.finish();

            // Set the relationship lists for the contacts in this batch
            QMap<quint32, QList<QContactRelationship> >::const_iterator rit = contactRelationships.constBegin(), rend = contactRelationships.constEnd();
            for ( ; rit != rend; ++rit) {
                quint32 contactId = rit.key();

                QMap<quint32, int>::iterator iit = contactIdIndex.find(contactId);
                if (iit != contactIdIndex.end()) {
                    int contactIndex = *iit;
                    QContact &contact(*(contacts->begin() + contactIndex));

                    // Set the relationships for this contact
                    QContactManagerEngine::setContactRelationships(&contact, rit.value());
                }
            }
        }

        contactsAvailable(*contacts);
    } while (query.isValid() && (maximumCount < 0));

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readDeletedContactIds(
        QList<QContactId> *contactIds,
        const QContactFilter &filter)
{
    QDateTime since;
    QString syncTarget;

    // The only queries we support regarding deleted contacts are for the IDs, possibly
    // intersected with a syncTarget detail filter
    if (filter.type() == QContactFilter::ChangeLogFilter) {
        const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(filter));
        since = changeLogFilter.since();
    } else if (filter.type() == QContactFilter::IntersectionFilter) {
        const QContactIntersectionFilter &intersectionFilter(static_cast<const QContactIntersectionFilter &>(filter));
        foreach (const QContactFilter &partialFilter, intersectionFilter.filters()) {
            const QContactFilter::FilterType filterType(partialFilter.type());

            if (filterType == QContactFilter::ChangeLogFilter) {
                const QContactChangeLogFilter &changeLogFilter(static_cast<const QContactChangeLogFilter &>(partialFilter));
                since = changeLogFilter.since();
            } else if (filterType == QContactFilter::ContactDetailFilter) {
                const QContactDetailFilter &detailFilter(static_cast<const QContactDetailFilter &>(partialFilter));
                if (filterOnField<QContactSyncTarget>(detailFilter, QContactSyncTarget::FieldSyncTarget)) {
                    syncTarget = detailFilter.value().toString();
                } else {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot readDeletedContactIds with unsupported detail filter type: %1").arg(detailFilter.detailType()));
                    return QContactManager::UnspecifiedError;
                }
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot readDeletedContactIds with invalid filter type: %1").arg(filterType));
                return QContactManager::UnspecifiedError;
            }
        }
    }

    QStringList restrictions;
    QVariantList bindings;
    if (!since.isNull()) {
        restrictions.append(QString::fromLatin1("deleted >= ?"));
        bindings.append(ContactsDatabase::dateTimeString(since.toUTC()));
    }
    if (!syncTarget.isNull()) {
        restrictions.append(QString::fromLatin1("syncTarget = ?"));
        bindings.append(syncTarget);
    }

    QString queryStatement(QString::fromLatin1("SELECT contactId FROM DeletedContacts"));
    if (!restrictions.isEmpty()) {
        queryStatement.append(QString::fromLatin1(" WHERE "));
        queryStatement.append(restrictions.takeFirst());
        if (!restrictions.isEmpty()) {
            queryStatement.append(QString::fromLatin1(" AND "));
            queryStatement.append(restrictions.takeFirst());
        }
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(queryStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare deleted contacts ids:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryStatement));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!query.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query deleted contacts ids\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryStatement));
        return QContactManager::UnspecifiedError;
    }

    do {
        for (int i = 0; i < ReportBatchSize && query.next(); ++i) {
            contactIds->append(ContactId::apiId(query.value(0).toUInt()));
        }
        contactIdsAvailable(*contactIds);
    } while (query.isValid());

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readContactIds(
        QList<QContactId> *contactIds,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order)
{
    QMutexLocker locker(m_database.accessMutex());

    // Is this a query on deleted contacts?
    if (deletedContactFilter(filter)) {
        return readDeletedContactIds(contactIds, filter);
    }

    // Use a dummy table name to identify any temporary tables we create
    const QString tableName(QString::fromLatin1("readContactIds"));

    m_database.clearTransientContactIdsTable(tableName);

    QString join;
    bool transientModifiedRequired = false;
    bool globalPresenceRequired = false;
    const QString orderBy = buildOrderBy(order, &join, &transientModifiedRequired, &globalPresenceRequired);

    bool failed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, m_database, tableName, &bindings, &failed, &transientModifiedRequired, &globalPresenceRequired);
    if (failed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter, m_database.aggregating());

    if (transientModifiedRequired || globalPresenceRequired) {
        // Provide the temporary transient state information to filter/sort on
        if (!m_database.populateTemporaryTransientState(transientModifiedRequired, globalPresenceRequired)) {
            return QContactManager::UnspecifiedError;
        }

        if (transientModifiedRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.Timestamps ON Contacts.contactId = temp.Timestamps.contactId"));
        }
        if (globalPresenceRequired) {
            join.append(QStringLiteral(" LEFT JOIN temp.GlobalPresenceStates ON Contacts.contactId = temp.GlobalPresenceStates.contactId"));
        }
    }

    QString queryString = QString(QLatin1String(
                "\n SELECT DISTINCT Contacts.contactId"
                "\n FROM Contacts %1"
                "\n %2")).arg(join).arg(where);
    if (!orderBy.isEmpty()) {
        queryString.append(QString::fromLatin1(" ORDER BY ") + orderBy);
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(queryString)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare contacts ids:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!query.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query contacts ids\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    } else {
        debugFilterExpansion("Contact IDs selection:", queryString, bindings);
    }

    do {
        for (int i = 0; i < ReportBatchSize && query.next(); ++i) {
            contactIds->append(ContactId::apiId(query.value(0).toUInt()));
        }
        contactIdsAvailable(*contactIds);
    } while (query.isValid());

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::getIdentity(
        ContactsDatabase::Identity identity, QContactId *contactId)
{
    QMutexLocker locker(m_database.accessMutex());

    if (identity == ContactsDatabase::SelfContactId) {
        // we don't allow setting the self contact id, it's always static
        *contactId = ContactId::apiId(selfId);
    } else {
        const QString identityId(QStringLiteral(
            " SELECT contactId"
            " FROM Identities"
            " WHERE identity = :identity"
        ));

        ContactsDatabase::Query query(m_database.prepare(identityId));
        query.bindValue(":identity", identity);
        if (!query.exec()) {
            query.reportError("Failed to fetch contact identity");
            return QContactManager::UnspecifiedError;
        }
        if (!query.next()) {
            *contactId = QContactId();
            return QContactManager::UnspecifiedError;
        } else {
            *contactId = ContactId::apiId(query.value<quint32>(0));
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readRelationships(
        QList<QContactRelationship> *relationships,
        const QString &type,
        const QContactId &first,
        const QContactId &second)
{
    QMutexLocker locker(m_database.accessMutex());

    QStringList whereStatements;
    QVariantList bindings;
    if (!type.isEmpty()) {
        whereStatements.append(QLatin1String("type = ?"));
        bindings.append(type);
    }

    quint32 firstId = ContactId::databaseId(first);
    if (firstId != 0) {
        whereStatements.append(QLatin1String("firstId = ?"));
        bindings.append(firstId);
    }

    quint32 secondId = ContactId::databaseId(second);
    if (secondId != 0) {
        whereStatements.append(QLatin1String("secondId = ?"));
        bindings.append(secondId);
    }

    const QString where = !whereStatements.isEmpty()
            ? QLatin1String("\n WHERE ") + whereStatements.join(QLatin1String(" AND "))
            : QString();

    QString statement = QLatin1String(
            "\n SELECT type, firstId, secondId"
            "\n FROM Relationships") + where + QLatin1String(";");

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare relationships query:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < bindings.count(); ++i)
        query.bindValue(i, bindings.at(i));

    if (!query.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query relationships: %1")
                .arg(query.lastError().text()));
        return QContactManager::UnspecifiedError;
    }

    while (query.next()) {
        QString type = query.value(0).toString();
        quint32 firstId = query.value(1).toUInt();
        quint32 secondId = query.value(2).toUInt();

        relationships->append(makeRelationship(type, firstId, secondId));
    }
    query.finish();

    return QContactManager::NoError;
}

bool ContactReader::fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values)
{
    QVariantList keyNames;

    QString statement(QString::fromLatin1("SELECT name, value, compressed FROM OOB WHERE name "));
    if (keys.isEmpty()) {
        statement.append(QString::fromLatin1("LIKE '%1:%%'").arg(scope));
    } else {
        const QChar colon(QChar::fromLatin1(':'));

        QString keyList;
        foreach (const QString &key, keys) {
            keyNames.append(scope + colon + key);
            keyList.append(QString::fromLatin1(keyList.isEmpty() ? "?" : ",?"));
        }
        statement.append(QString::fromLatin1("IN (%1)").arg(keyList));
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB query:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return false;
    }

    foreach (const QVariant &name, keyNames) {
        query.addBindValue(name);
    }

    if (!query.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query OOB: %1")
                .arg(query.lastError().text()));
        return false;
    }
    while (query.next()) {
        const QString name(query.value(0).toString());
        const QVariant value(query.value(1));
        const quint32 compressed(query.value(2).toUInt());

        const QString key(name.mid(scope.length() + 1));
        if (compressed > 0) {
            QByteArray compressedData(value.value<QByteArray>());
            if (compressed == 1) {
                // QByteArray data
                values->insert(key, QVariant(qUncompress(compressedData)));
            } else if (compressed == 2) {
                // QString data
                values->insert(key, QVariant(QString::fromUtf8(qUncompress(compressedData))));
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid compression type for OOB data:%1, key:%2")
                        .arg(compressed).arg(key));
            }
        } else {
            values->insert(key, value);
        }
    }
    query.finish();

    return true;
}

bool ContactReader::fetchOOBKeys(const QString &scope, QStringList *keys)
{
    QString statement(QString::fromLatin1("SELECT name FROM OOB WHERE name LIKE '%1:%%'").arg(scope));

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB query:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return false;
    }

    if (!query.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query OOB: %1")
                .arg(query.lastError().text()));
        return false;
    }
    while (query.next()) {
        const QString name(query.value(0).toString());
        keys->append(name.mid(scope.length() + 1));
    }
    query.finish();

    return true;
}

void ContactReader::contactsAvailable(const QList<QContact> &)
{
}

void ContactReader::contactIdsAvailable(const QList<QContactId> &)
{
}
