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

#include "qtcontacts-extensions.h"
#include "QContactOriginMetadata"
#include "QContactStatusFlags"

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
#ifdef USING_QTPIM
#include <QContactIdFilter>
#else
#include <QContactLocalIdFilter>
#endif
#include <QContactChangeLogFilter>
#include <QContactUnionFilter>
#include <QContactIntersectionFilter>

#include <QContactManagerEngine>

#include <QSqlError>
#include <QVector>

#include <QtDebug>

#ifdef USING_QTPIM
using namespace Conversion;
#endif

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
#ifdef USING_QTPIM
    int field;
#else
    QLatin1String field;
#endif
    const char *column;
    FieldType fieldType;
};

#ifdef USING_QTPIM
static void setValue(QContactDetail *detail, int key, const QVariant &value)
{
    if (value.type() != QVariant::String || !value.toString().isEmpty())
        detail->setValue(key, value);
}
#else
template<int N> static void setValue(
        QContactDetail *detail, const QLatin1Constant<N> &key, const QVariant &value)
{
    if (value.type() != QVariant::String || !value.toString().isEmpty())
        detail->setValue(key, value);
}
#endif

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
#ifdef USING_QTPIM
    { QContactName__FieldCustomLabel, "customLabel", StringField }
#else
    { QContactName::FieldCustomLabel, "customLabel", StringField }
#endif
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
    // No specific field; tests hasPhoneNumber/hasEmailAddress/hasOnlineAccount/isOnline
    { QContactStatusFlags::FieldFlags, "", OtherField }
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
#ifdef USING_QTPIM
    QStringList subTypeNames(query->value(offset + 6).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes     , QVariant::fromValue<QList<int> >(Address::subTypeList(subTypeNames)));
#else
    setValue(detail, T::FieldSubTypes     , query->value(offset + 6).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
#endif

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

    setValue(detail, T::FieldOriginalDate, query->value(offset + 0));
    setValue(detail, T::FieldCalendarId  , query->value(offset + 1));
#ifdef USING_QTPIM
    setValue(detail, T::FieldSubType     , QVariant::fromValue<int>(Anniversary::subType(query->value(offset + 2).toString())));
#else
    setValue(detail, T::FieldSubType     , query->value(offset + 2));
#endif
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

    setValue(detail, T::FieldBirthday  , query->value(offset + 0));
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
#ifdef USING_QTPIM
    setValue(detail, T::FieldProtocol       , QVariant::fromValue<int>(OnlineAccount::protocol(query->value(offset + 2).toString())));
#else
    setValue(detail, T::FieldProtocol       , query->value(offset + 2));
#endif
    setValue(detail, T::FieldServiceProvider, query->value(offset + 3));
    setValue(detail, T::FieldCapabilities   , stringListValue(query->value(offset + 4)));

#ifdef USING_QTPIM
    QStringList subTypeNames(query->value(offset + 5).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(OnlineAccount::subTypeList(subTypeNames)));
#else
    setValue(detail, T::FieldSubTypes       , stringListValue(query->value(offset + 5)));
#endif

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

#ifdef USING_QTPIM
    QStringList subTypeNames(query->value(offset + 1).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldSubTypes, QVariant::fromValue<QList<int> >(PhoneNumber::subTypeList(subTypeNames)));
#else
    setValue(detail, T::FieldSubTypes, stringListValue(query->value(offset + 1)));
#endif

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
    setValue(detail, T::FieldTimestamp    , query->value(offset + 1));
    setValue(detail, T::FieldNickname     , query->value(offset + 2));
    setValue(detail, T::FieldCustomMessage, query->value(offset + 3));
}

static void setValues(QContactGlobalPresence *detail, QSqlQuery *query, const int offset)
{
    typedef QContactPresence T;

    setValue(detail, T::FieldPresenceState, query->value(offset + 0).toInt());
    setValue(detail, T::FieldTimestamp    , query->value(offset + 1));
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
#ifdef USING_QTPIM
    setValue(detail, T::FieldSubType, QVariant::fromValue<int>(Url::subType(query->value(offset + 1).toString())));
#else
    setValue(detail, T::FieldSubType, query->value(offset + 1));
#endif
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

#ifdef USING_QTPIM
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
#endif

#ifdef USING_QTPIM
static QMap<QString, int> contextTypes()
{
    QMap<QString, int> rv;

    rv.insert(QString::fromLatin1("Home"), QContactDetail::ContextHome);
    rv.insert(QString::fromLatin1("Work"), QContactDetail::ContextWork);
    rv.insert(QString::fromLatin1("Other"), QContactDetail::ContextOther);
    rv.insert(QString::fromLatin1("Default"), QContactDetail__ContextDefault);
    rv.insert(QString::fromLatin1("Large"), QContactDetail__ContextLarge);

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
#endif

template <typename T> static void readDetail(
        quint32 contactId, QContact *contact, QSqlQuery *query, bool syncable, quint32 &currentId)
{
    do {
        T detail;

        const QString detailUriValue = query->value(0).toString();
        const QString linkedDetailUrisValue = query->value(1).toString();
        const QString contextValue = query->value(2).toString();
        const int accessConstraints = query->value(3).toInt();
        const QString provenance = query->value(4).toString();
        const bool modifiable = query->value(5).toBool();
        /* Unused:
        const quint32 detailId = query->value(6).toUInt();
        const quint32 detailContactId = query->value(7).toUInt();
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
#ifdef USING_QTPIM
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
#else
            setValue(&detail,
                     QContactDetail::FieldContext,
                     contextValue.split(QLatin1Char(';'), QString::SkipEmptyParts));
#endif
        }
        QContactManagerEngine::setDetailAccessConstraints(&detail, static_cast<QContactDetail::AccessConstraints>(accessConstraints));
        setValue(&detail, QContactDetail__FieldProvenance, provenance);

        // Only report modifiable state for non-local, non-aggregate contacts
        if (syncable)
            setValue(&detail, QContactDetail__FieldModifiable, modifiable);

        setValues(&detail, query, 8);

        contact->saveDetail(&detail);
    } while (query->next() && (currentId = query->value(7).toUInt()) == contactId);
}

static QContactRelationship makeRelationship(const QString &type, quint32 firstId, quint32 secondId)
{
    QContactRelationship relationship;
    relationship.setRelationshipType(type);

#ifdef USING_QTPIM
    QContact first, second;
    first.setId(ContactId::apiId(firstId));
    second.setId(ContactId::apiId(secondId));
    relationship.setFirst(first);
    relationship.setSecond(second);
#else
    relationship.setFirst(ContactId::contactId(ContactId::apiId(firstId)));
    relationship.setSecond(ContactId::contactId(ContactId::apiId(secondId)));
#endif

    return relationship;
}

static void readRelationshipTable(quint32 contactId, QContact *contact, QSqlQuery *query, bool syncable, quint32 &currentId)
{
    Q_UNUSED(syncable)

    QList<QContactRelationship> currContactRelationships;

    do {
        QString type = query->value(1).toString();
        quint32 firstId = query->value(2).toUInt();
        quint32 secondId = query->value(3).toUInt();

        currContactRelationships.append(makeRelationship(type, firstId, secondId));
    } while (query->next() && (currentId = query->value(0).toUInt()) == contactId);

    QContactManagerEngine::setContactRelationships(contact, currContactRelationships);
}

typedef void (*ReadDetail)(quint32 contactId, QContact *contact, QSqlQuery *query, bool syncable, quint32 &currentId);

struct DetailInfo
{
#ifdef USING_QTPIM
    QContactDetail::DetailType detail;
    const char *detailName;
#else
    const QLatin1String detail;
#endif
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

#ifdef USING_QTPIM
template <typename T> QContactDetail::DetailType detailIdentifier() { return T::Type; }
#else
template <typename T> const QLatin1String detailIdentifier() { return T::DefinitionName; }
#endif

#define PREFIX_LENGTH 8
#ifdef USING_QTPIM
#define DEFINE_DETAIL(Detail, Table, fields, join) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, #Table, fields, lengthOf(fields), join, readDetail<Detail> }

#define DEFINE_DETAIL_PRIMARY_TABLE(Detail, fields) \
    { detailIdentifier<Detail>(), #Detail + PREFIX_LENGTH, 0, fields, lengthOf(fields), false, 0 }
#else
#define DEFINE_DETAIL(Detail, Table, fields, join) \
    { detailIdentifier<Detail>(), #Table, fields, lengthOf(fields), join, readDetail<Detail> }

#define DEFINE_DETAIL_PRIMARY_TABLE(Detail, fields) \
    { detailIdentifier<Detail>(), 0, fields, lengthOf(fields), false, 0 }
#endif

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
#ifdef USING_QTPIM
    DEFINE_DETAIL(QContactExtendedDetail, ExtendedDetails, extendedDetailFields, false),
#endif
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

template<typename T1, typename T2>
static bool matchOnType(const T1 &filter, T2 type)
{
#ifdef USING_QTPIM
    return filter.detailType() == type;
#else
    return filter.detailDefinitionName() == type;
#endif
}

template<typename T, typename F>
static bool filterOnField(const QContactDetailFilter &filter, F field)
{
#ifdef USING_QTPIM
    return (filter.detailType() == T::Type &&
            filter.detailField() == field);
#else
    return (filter.detailDefinitionName() == T::DefinitionName &&
            filter.detailFieldName() == field);
#endif
}

template<typename F>
static bool validFilterField(F filter)
{
#ifdef USING_QTPIM
    return (filter.detailField() != -1);
#else
    return !filter.detailFieldName().isEmpty();
#endif
}

template<typename F>
#ifdef USING_QTPIM
static int filterField(F filter) { return filter.detailField(); }
#else
static QString filterField(F filter) { return filter.detailFieldName(); }
#endif

#ifdef USING_QTPIM
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
#endif

static QString dateString(bool dateOnly, const QDateTime &qdt)
{
    if (dateOnly) {
        //return QString(QLatin1String("date('%1')")).arg(qdt.toUTC().toString(Qt::ISODate));
        return qdt.toUTC().toString(Qt::ISODate).mid(0, 10); // 'yyyy-MM-dd'
    }

    // note: quoting (via QString("'%1'").arg(...)) causes unit test failures
    // because we bind the resultant value rather than use substring replacement
    // and a bound string value is quoted by Qt.
    return qdt.toUTC().toString(Qt::ISODate);
}

static QString dateString(const DetailInfo &detail, const QDateTime &qdt)
{
#ifdef USING_QTPIM
    if (detail.detail == QContactBirthday::Type
            || detail.detail == QContactAnniversary::Type) {
#else
    if (detail.detail == QContactBirthday::DefinitionName
            || detail.detail == QContactAnniversary::DefinitionName) {
#endif
        // just interested in the date, not the whole date time
        return dateString(true, qdt);
    }

    return dateString(false, qdt);
}


static QString buildWhere(const QContactDetailFilter &filter, QVariantList *bindings, bool *failed)
{
    if (filter.matchFlags() & QContactFilter::MatchKeypadCollation) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with filter requiring keypad collation"));
        return QLatin1String("FAILED");
    }

    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!matchOnType(filter, detail.detail))
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
                                                 QContactStatusFlags::IsOnline };
                    static const char *flagColumns[] = { "hasPhoneNumber",
                                                         "hasEmailAddress",
                                                         "hasOnlineAccount",
                                                         "isOnline" };

                    quint64 flagsValue = filter.value().value<quint64>();

                    QStringList clauses;
                    if (filter.matchFlags() == QContactFilter::MatchExactly) {
                        for (int i  = 0; i < lengthOf(flags); ++i) {
                            clauses.append(QString::fromLatin1("%1 = %2").arg(flagColumns[i]).arg((flagsValue & flags[i]) ? 1 : 0));
                        }
                    } else if (filter.matchFlags() == QContactFilter::MatchContains) {
                        for (int i  = 0; i < lengthOf(flags); ++i) {
                            if (flagsValue & flags[i]) {
                                clauses.append(QString::fromLatin1("%1 = 1").arg(flagColumns[i]));
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
#ifdef USING_QTPIM
                const QVariant &v(filter.value());
                if (dateField) {
                    bindValue = dateString(detail, v.toDateTime());
                } else if (!stringField && (v.type() == QVariant::Bool)) {
                    // Convert to "1"/"0" rather than "true"/"false"
                    bindValue = QString::number(v.toBool() ? 1 : 0);
                } else {
                    stringValue = convertFilterValueToString(filter, stringValue);
#endif
                    bindValue = caseInsensitive ? stringValue.toLower() : stringValue;
#ifdef USING_QTPIM
                }
#endif
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

            return detail.where().arg(comparison.arg(column.isEmpty() ? field.column : column));
        }
    }

    *failed = true;
#ifdef USING_QTPIM
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown DetailFilter detail: %1").arg(filter.detailType()));
#else
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown DetailFilter detail: %1").arg(filter.detailDefinitionName()));
#endif
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactDetailRangeFilter &filter, QVariantList *bindings, bool *failed)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!matchOnType(filter, detail.detail))
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
#ifdef USING_QTPIM
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown DetailRangeFilter detail: %1").arg(filter.detailType()));
#else
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown DetailRangeFilter detail: %1").arg(filter.detailDefinitionName()));
#endif
    return QLatin1String("FALSE");
}

#ifdef USING_QTPIM
static QString buildWhere(const QContactIdFilter &filter, QVariantList *bindings, bool *failed)
#else
static QString buildWhere(const QContactLocalIdFilter &filter, QVariantList *bindings, bool *failed)
#endif
{
    QList<quint32> dbIds;
    foreach (const QContactIdType &id, filter.ids()) {
        dbIds.append(ContactId::databaseId(id));
    }

    if (dbIds.isEmpty()) {
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with empty contact ID list"));
        return QLatin1String("FALSE");
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
#ifdef USING_QTPIM
    QContactId rci = filter.relatedContact().id();
#else
    QContactId rci = filter.relatedContactId();
#endif

    QContactRelationship::Role rcr = filter.relatedContactRole();
    QString rt = filter.relationshipType();

    quint32 dbId = ContactId::databaseId(rci);

#ifdef USING_QTPIM
    if (!rci.managerUri().isEmpty() && !rci.managerUri().startsWith(QString::fromLatin1("qtcontacts:org.nemomobile.contacts.sqlite:"))) {
#else
    if (!rci.managerUri().isEmpty() && rci.managerUri() != QLatin1String("org.nemomobile.contacts.sqlite")) {
#endif
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

static QString buildWhere(const QContactChangeLogFilter &filter, QVariantList *bindings, bool *failed)
{
    static const QString statement(QLatin1String("Contacts.%1 >= ?"));
    bindings->append(dateString(false, filter.since()));
    switch (filter.eventType()) {
        case QContactChangeLogFilter::EventAdded:
            return statement.arg(QLatin1String("created"));
        case QContactChangeLogFilter::EventChanged:
            return statement.arg(QLatin1String("modified"));
        default: break;
    }

    *failed = true;
    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with changelog filter on removed timestamps"));
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactFilter &filter, QVariantList *bindings, bool *failed);

static QString buildWhere(const QContactUnionFilter &filter, QVariantList *bindings, bool *failed)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, bindings, failed);
        if (!*failed && !fragment.isEmpty()) {
            fragments.append(fragment);
        }
    }

    return QString::fromLatin1("( %1 )").arg(fragments.join(QLatin1String(" OR ")));
}

static QString buildWhere(const QContactIntersectionFilter &filter, QVariantList *bindings, bool *failed)
{
    const QList<QContactFilter> filters  = filter.filters();
    if (filters.isEmpty())
        return QString();

    QStringList fragments;
    foreach (const QContactFilter &filter, filters) {
        const QString fragment = buildWhere(filter, bindings, failed);
        if (filter.type() != QContactFilter::DefaultFilter && !*failed) {
            // default filter gets special (permissive) treatment by the intersection filter.
            fragments.append(fragment.isEmpty() ? QLatin1String("NULL") : fragment);
        }
    }

    return fragments.join(QLatin1String(" AND "));
}

static QString buildWhere(const QContactFilter &filter, QVariantList *bindings, bool *failed)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
        return QString();
    case QContactFilter::ContactDetailFilter:
        return buildWhere(static_cast<const QContactDetailFilter &>(filter), bindings, failed);
    case QContactFilter::ContactDetailRangeFilter:
        return buildWhere(static_cast<const QContactDetailRangeFilter &>(filter), bindings, failed);
    case QContactFilter::ChangeLogFilter:
        return buildWhere(static_cast<const QContactChangeLogFilter &>(filter), bindings, failed);
    case QContactFilter::RelationshipFilter:
        return buildWhere(static_cast<const QContactRelationshipFilter &>(filter), bindings, failed);
    case QContactFilter::IntersectionFilter:
        return buildWhere(static_cast<const QContactIntersectionFilter &>(filter), bindings, failed);
    case QContactFilter::UnionFilter:
        return buildWhere(static_cast<const QContactUnionFilter &>(filter), bindings, failed);
#ifdef USING_QTPIM
    case QContactFilter::IdFilter:
        return buildWhere(static_cast<const QContactIdFilter &>(filter), bindings, failed);
#else
    case QContactFilter::LocalIdFilter:
        return buildWhere(static_cast<const QContactLocalIdFilter &>(filter), bindings, failed);
#endif
    default:
        *failed = true;
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot buildWhere with unknown filter type: %1").arg(filter.type()));
        return QLatin1String("FALSE");
    }
}

#ifdef USING_QTPIM
static int sortField(const QContactSortOrder &sort) { return sort.detailField(); }
#else
static QString sortField(const QContactSortOrder &sort) { return sort.detailFieldName(); }
#endif

static QString buildOrderBy(const QContactSortOrder &order, QStringList *joins)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!matchOnType(order, detail.detail))
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];
            if (sortField(order) != field.field)
                continue;

            QString collate = (order.caseSensitivity() == Qt::CaseSensitive)
                    ? QLatin1String("COLLATE RTRIM")
                    : QLatin1String("COLLATE NOCASE");
            QString direction = (order.direction() == Qt::AscendingOrder)
                    ? QLatin1String("ASC")
                    : QLatin1String("DESC");
            QString blanksLocation = (order.blankPolicy() == QContactSortOrder::BlanksLast)
                    ? QLatin1String("CASE WHEN %2.%3 IS NULL OR %2.%3 = '' THEN 1 ELSE 0 END,")
                    : QLatin1String("CASE WHEN %2.%3 IS NULL OR %2.%3 = '' THEN 0 ELSE 1 END,");

            if (detail.join) {
                QString join = QString(QLatin1String(
                        "LEFT JOIN %1 ON Contacts.contactId = %1.contactId"))
                        .arg(QLatin1String(detail.table));

                if (!joins->contains(join))
                    joins->append(join);

                return QString(QLatin1String("%1 %2.%3 %4 %5"))
                        .arg(blanksLocation)
                        .arg(QLatin1String(detail.table))
                        .arg(QLatin1String(field.column))
                        .arg(collate).arg(direction);
            } else if (!detail.table) {
                return QString(QLatin1String("%1 %2.%3 %4 %5"))
                        .arg(blanksLocation)
                        .arg(QLatin1String("Contacts"))
                        .arg(QLatin1String(field.column))
                        .arg(collate).arg(direction);
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("UNSUPPORTED SORTING: no join and not primary table for ORDER BY in query with: %1, %2")
#ifdef USING_QTPIM
                           .arg(order.detailType()).arg(order.detailField()));
#else
                           .arg(order.detailDefinitionName()).arg(order.detailFieldName()));
#endif
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
        QString fragment = buildOrderBy(sort, &joins);
        if (!fragment.isEmpty()) {
            fragments.append(buildOrderBy(sort, &joins));
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
    quint32 currentId;
};

namespace {

// The selfId is fixed - DB ID 1 is the 'self' local contact, and DB ID 2 is the aggregate
static const QContactIdType selfId(ContactId::apiId(2));

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
#ifdef USING_QTPIM
bool includesSelfId(const QContactIdFilter &filter)
#else
bool includesSelfId(const QContactLocalIdFilter &filter)
#endif
{
    return filter.ids().contains(selfId);
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
#ifdef USING_QTPIM
    case QContactFilter::IdFilter:
        return includesSelfId(static_cast<const QContactIdFilter &>(filter));
#else
    case QContactFilter::LocalIdFilter:
        return includesSelfId(static_cast<const QContactLocalIdFilter &>(filter));
#endif

    default:
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot includesSelfId with unknown filter type %1").arg(filter.type()));
        return false;
    }
}

QString expandWhere(const QString &where, const QContactFilter &filter)
{
    QString preamble(QLatin1String("WHERE "));

    // remove the self contact, unless specifically included
    bool includesSelfContactId = includesSelfId(filter);
    if (!includesSelfContactId) {
        preamble += QLatin1String("Contacts.contactId > 2 AND ");
    }

    // some (union) filters can add spurious braces around empty expressions
    QString strippedWhere = where;
    strippedWhere.remove(QChar('('));
    strippedWhere.remove(QChar(')'));
    strippedWhere.remove(QChar(' '));

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    // by default, we only return "aggregate" contacts, and we don't return the self contact (2)
    if (strippedWhere.isEmpty()) {
        return preamble + QLatin1String("Contacts.syncTarget = 'aggregate'");
    } else if (!where.contains("syncTarget")) {
        return preamble + QLatin1String("Contacts.syncTarget = 'aggregate' AND ") + where;
    } else { // Unless they explicitly specify a syncTarget criterium
        return preamble + where;
    }
#else
    if (strippedWhere.isEmpty()) {
        if (!includesSelfContactId) {
            return QLatin1String("WHERE Contacts.contactId > 2");
        } else {
            return QString();
        }
    } else {
        return preamble + where;
    }
#endif
}

}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order,
        const QContactFetchHint &fetchHint)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    QString join;
    const QString orderBy = buildOrderBy(order, &join);
    bool whereFailed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, &bindings, &whereFailed);
    if (whereFailed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter);

    QContactManager::Error error = QContactManager::NoError;
    if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, table, join, where, orderBy, bindings)) {
        error = QContactManager::UnspecifiedError;
    } else {
        error = queryContacts(table, contacts, fetchHint);
    }

    return error;
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<QContactIdType> &contactIds,
        const QContactFetchHint &fetchHint)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    QVariantList boundIds;
    for (int i = 0; i < contactIds.size(); ++i) {
        boundIds.append(ContactId::databaseId(contactIds.at(i)));
    }

    contacts->reserve(contactIds.size());

    QContactManager::Error error = QContactManager::NoError;
    if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, table, boundIds)) {
        error = QContactManager::UnspecifiedError;
    } else {
        error = queryContacts(table, contacts, fetchHint);
    }

    // the ordering of the queried contacts is identical to
    // the ordering of the input contact ids list.
    int contactIdsSize = contactIds.size();
    int contactsSize = contacts->size();
    if (contactIdsSize != contactsSize) {
        for (int i = 0; i < contactIdsSize; ++i) {
#ifdef USING_QTPIM
            if (i >= contactsSize || (*contacts)[i].id() != contactIds[i]) {
#else
            if (i >= contactsSize || (*contacts)[i].localId() != contactIds[i]) {
#endif
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
        const QString &tableName, QList<QContact> *contacts, const QContactFetchHint &fetchHint)
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

    const QString tableTemplate = QString(QLatin1String(
            "\n SELECT"
            "\n  Details.detailUri,"
            "\n  Details.linkedDetailUris,"
            "\n  Details.contexts,"
            "\n  Details.accessConstraints,"
            "\n  Details.provenance,"
            "\n  Details.modifiable,"
            "\n  %2.*"
            "\n FROM temp.%1"
            "\n  INNER JOIN %2 ON temp.%1.contactId = %2.contactId"
            "\n  LEFT JOIN Details ON %2.detailId = Details.detailId AND Details.detail = :detail"
            "\n ORDER BY temp.%1.rowId ASC;")).arg(tableName);

#ifdef USING_QTPIM
    const ContactWriter::DetailList &details = fetchHint.detailTypesHint();
#else
    const ContactWriter::DetailList &details = fetchHint.detailDefinitionsHint();
#endif

    QList<Table> tables;
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (!detail.read)
            continue;

        if (details.isEmpty() || details.contains(detail.detail)) {
            // we need to query this particular detail table
            // use cached prepared queries if available, else prepare and cache query.
            bool haveCachedQuery = m_cachedDetailTableQueries[tableName].contains(detail.table);
            Table table = {
                haveCachedQuery ? m_cachedDetailTableQueries[tableName].value(detail.table) : QSqlQuery(m_database),
                detail.read,
                0
            };

            if (!haveCachedQuery) {
                // have to prepare the query.
                const QString tableQueryStatement(tableTemplate.arg(QLatin1String(detail.table)));
                table.query.setForwardOnly(true);
                if (!table.query.prepare(tableQueryStatement)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare table %1:\n%2\n%3")
                            .arg(detail.table)
                            .arg(tableQueryStatement)
                            .arg(table.query.lastError().text()));
                } else {
                    m_cachedDetailTableQueries[tableName].insert(detail.table, table.query);
                    haveCachedQuery = true;
                }
            }

            if (haveCachedQuery) {
#ifdef USING_QTPIM
                table.query.bindValue(0, QString::fromLatin1(detail.detailName));
#else
                table.query.bindValue(0, detail.detail);
#endif
                if (!table.query.exec()) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query table %1:\n%2")
                            .arg(detail.table)
                            .arg(table.query.lastError().text()));
                } else if (table.query.next()) {
                    table.currentId = table.query.value(7).toUInt();
                    tables.append(table);
                }
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

        for (int i = 0; i < batchSize && query.next(); ++i) {
            quint32 dbId = query.value(0).toUInt();
            QContact contact;

            QContactId id(ContactId::contactId(ContactId::apiId(dbId)));
            contact.setId(id);

            iidList.append(QString::number(dbId));
            contactIdIndex.insert(dbId, contactCount + i);

            QString persistedDL = query.value(1).toString();
            if (!persistedDL.isEmpty())
#ifdef USING_QTPIM
                ContactsEngine::setContactDisplayLabel(&contact, persistedDL);
#else
                QContactManagerEngine::setContactDisplayLabel(&contact, persistedDL);
#endif

            QContactName name;
            setValue(&name, QContactName::FieldFirstName  , query.value(2));
            // ignore lowerFirstName
            setValue(&name, QContactName::FieldLastName   , query.value(4));
            // ignore lowerLastName
            setValue(&name, QContactName::FieldMiddleName , query.value(6));
            setValue(&name, QContactName::FieldPrefix     , query.value(7));
            setValue(&name, QContactName::FieldSuffix     , query.value(8));
#ifdef USING_QTPIM
            setValue(&name, QContactName__FieldCustomLabel, query.value(9));
#else
            setValue(&name, QContactName::FieldCustomLabel, query.value(9));
#endif
            if (!name.isEmpty())
                contact.saveDetail(&name);

            const QString syncTarget(query.value(10).toString());

            QContactSyncTarget starget;
            setValue(&starget, QContactSyncTarget::FieldSyncTarget, syncTarget);
            if (!starget.isEmpty())
                contact.saveDetail(&starget);

            QContactTimestamp timestamp;
            setValue(&timestamp, QContactTimestamp::FieldCreationTimestamp    , query.value(11));
            setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, query.value(12));
            if (!timestamp.isEmpty())
                contact.saveDetail(&timestamp);

            QContactGender gender;
#ifdef USING_QTPIM
            // Gender is an enum in qtpim
            QString genderText = query.value(13).toString();
            if (genderText.startsWith(QChar::fromLatin1('f'), Qt::CaseInsensitive)) {
                gender.setGender(QContactGender::GenderFemale);
            } else if (genderText.startsWith(QChar::fromLatin1('m'), Qt::CaseInsensitive)) {
                gender.setGender(QContactGender::GenderMale);
            } else {
                gender.setGender(QContactGender::GenderUnspecified);
            }
#else
            setValue(&gender, QContactGender::FieldGender, query.value(13));
#endif
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
            QContactManagerEngine::setDetailAccessConstraints(&flags, QContactDetail::ReadOnly | QContactDetail::Irremovable);
            contact.saveDetail(&flags);

            contacts->append(contact);

            bool syncable = !syncTarget.isEmpty() &&
                            (syncTarget != aggregateSyncTarget) &&
                            (syncTarget != localSyncTarget) &&
                            (syncTarget != wasLocalSyncTarget);
            syncableContact.append(syncable);
        }

        for (int j = 0; j < tables.count(); ++j) {
            Table &table = tables[j];

            QList<bool>::iterator sit = syncableContact.begin();
            QList<QContact>::iterator it = contacts->begin() + contactCount;
            for (QList<QContact>::iterator end = contacts->end(); it != end; ++it, ++sit) {
                QContact &contact(*it);
                quint32 contactId = ContactId::databaseId(contact.id());

                if (table.query.isValid() && (table.currentId == contactId)) {
                    table.read(contactId, &contact, &table.query, *sit, table.currentId);
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

    query.finish();
    for (int k = 0; k < tables.count(); ++k) {
        Table &table = tables[k];
        table.query.finish();
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readContactIds(
        QList<QContactIdType> *contactIds,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    QString join;
    const QString orderBy = buildOrderBy(order, &join);
    bool failed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, &bindings, &failed);

    if (failed) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create WHERE expression: invalid filter specification"));
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter);

    const QString queryString = QString(QLatin1String(
                "\n SELECT DISTINCT Contacts.contactId"
                "\n FROM Contacts %1"
                "\n %2"
                "\n ORDER BY %3;")).arg(join).arg(where).arg(orderBy);

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
        ContactsDatabase::Identity identity, QContactIdType *contactId)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (identity == ContactsDatabase::SelfContactId) {
        // we don't allow setting the self contact id, it's always static
        *contactId = selfId;
    } else {
        QSqlQuery query(m_database);
        query.setForwardOnly(true);
        query.prepare(QLatin1String(
                "\n SELECT contactId"
                "\n FROM Identities"
                "\n WHERE identity = :identity"));
        query.bindValue(0, identity);

        if (query.exec() && query.next()) {
            *contactId = ContactId::apiId(query.value(0).toUInt());
        } else {
            *contactId = QContactIdType();
            return QContactManager::UnspecifiedError;
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
    QMutexLocker locker(ContactsDatabase::accessMutex());

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

void ContactReader::contactsAvailable(const QList<QContact> &)
{
}

void ContactReader::contactIdsAvailable(const QList<QContactIdType> &)
{
}
