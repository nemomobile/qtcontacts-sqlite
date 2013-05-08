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

#include <QContactManagerEngine>

#include <QSqlError>
#include <QVector>

#include <QtDebug>

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
    QLatin1String field;
    const char *column;
    FieldType fieldType;
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
    { QContactName::FieldCustomLabel, "customLabel", StringField }
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

static const FieldInfo addressFields[] =
{
    { QContactAddress::FieldStreet, "street", StringField },
    { QContactAddress::FieldPostOfficeBox, "postOfficeBox", StringField },
    { QContactAddress::FieldRegion, "region", StringField },
    { QContactAddress::FieldLocality, "locality", StringField },
    { QContactAddress::FieldPostcode, "postCode", StringField },
    { QContactAddress::FieldCountry, "country", StringField },
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
    setValue(detail, T::FieldSubType     , query->value(offset + 2));
}

QTM_BEGIN_NAMESPACE
Q_DECLARE_LATIN1_CONSTANT(QContactAvatar__FieldAvatarMetadata, "AvatarMetadata") = { "AvatarMetadata" };
QTM_END_NAMESPACE
static const FieldInfo avatarFields[] =
{
    { QContactAvatar::FieldImageUrl, "imageUrl", StringField },
    { QContactAvatar::FieldVideoUrl, "videoUrl", StringField },
    { QContactAvatar__FieldAvatarMetadata, "avatarMetadata", StringField }
};

static void setValues(QContactAvatar *detail, QSqlQuery *query, const int offset)
{
    typedef QContactAvatar T;

    setValue(detail, T::FieldImageUrl, query->value(offset + 0));
    setValue(detail, T::FieldVideoUrl, query->value(offset + 1));
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
    { QContactOnlineAccount::FieldCapabilities, "capabilities", StringField },
    { QContactOnlineAccount::FieldSubTypes, "subTypes", StringListField },
    { QLatin1String("AccountPath")    , "accountPath", StringField },
    { QLatin1String("AccountIconPath"), "accountIconPath", StringField },
    { QLatin1String("Enabled")        , "enabled", BooleanField }
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
    setValue(detail, T::FieldDepartment, query->value(offset + 4).toString().split(QLatin1Char(';'), QString::SkipEmptyParts));
    setValue(detail, T::FieldLogoUrl   , QUrl(query->value(offset + 5).toString()));
}

static const FieldInfo phoneNumberFields[] =
{
    { QContactPhoneNumber::FieldNumber, "phoneNumber", StringField },
    { QContactPhoneNumber::FieldSubTypes, "subTypes", StringListField },
    { QContactPhoneNumber::FieldContext, "context", StringField },
    { QLatin1String("NormalizedNumber"), "normalizedNumber", StringField }
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
    { QContactPresence::FieldPresenceState, "presenceState", StringField },
    { QContactPresence::FieldTimestamp, "timestamp", DateField },
    { QContactPresence::FieldNickname, "nickname", StringField },
    { QContactPresence::FieldCustomMessage, "customMessage", StringField }
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
    { QContactRingtone::FieldAudioRingtoneUrl, "audioRingtone", StringField },
    { QContactRingtone::FieldVideoRingtoneUrl, "videoRingtone", StringField }
};

static void setValues(QContactRingtone *detail, QSqlQuery *query, const int offset)
{
    typedef QContactRingtone T;

    setValue(detail, T::FieldAudioRingtoneUrl, QUrl(query->value(offset + 0).toString()));
    setValue(detail, T::FieldVideoRingtoneUrl, QUrl(query->value(offset + 1).toString()));
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

    setValue(detail, T::FieldUrl    , query->value(offset + 0));
    setValue(detail, T::FieldSubType, query->value(offset + 1));
}

static const FieldInfo tpMetadataFields[] =
{
    { QLatin1String("ContactId"), "telepathyId", StringField },
    { QLatin1String("AccountId"), "accountId", StringField },
    { QLatin1String("AccountEnabled"), "accountEnabled", BooleanField }
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
        int accessConstraints = query->value(3).toInt();

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
        QContactManagerEngine::setDetailAccessConstraints(&detail, static_cast<QContactDetail::AccessConstraints>(accessConstraints));
        setValues(&detail, query, 6);

        contact->saveDetail(&detail);
    } while (query->next() && (currentId = query->value(5).toUInt()) == contactId);
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
                ? QString(QLatin1String("Contacts.contactId IN (SELECT contactId FROM %1 WHERE %2)")).arg(QLatin1String(table))
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
    DEFINE_DETAIL_PRIMARY_TABLE(QContactDisplayLabel, displayLabelFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactName,         nameFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactSyncTarget,   syncTargetFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactTimestamp,    timestampFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactGender,       genderFields),
    DEFINE_DETAIL_PRIMARY_TABLE(QContactFavorite,     favoriteFields),
    DEFINE_DETAIL(QContactAddress       , Addresses      , addressFields      , true),
    DEFINE_DETAIL(QContactAnniversary   , Anniversaries  , anniversaryFields  , false),
    DEFINE_DETAIL(QContactAvatar        , Avatars        , avatarFields       , false),
    DEFINE_DETAIL(QContactBirthday      , Birthdays      , birthdayFields     , true),
    DEFINE_DETAIL(QContactEmailAddress  , EmailAddresses , emailAddressFields , true),
    DEFINE_DETAIL(QContactGuid          , Guids          , guidFields         , true),
    DEFINE_DETAIL(QContactHobby         , Hobbies        , hobbyFields        , false),
    DEFINE_DETAIL(QContactNickname      , Nicknames      , nicknameFields     , true),
    DEFINE_DETAIL(QContactNote          , Notes          , noteFields         , false),
    DEFINE_DETAIL(QContactOnlineAccount , OnlineAccounts , onlineAccountFields, true),
    DEFINE_DETAIL(QContactOrganization  , Organizations  , organizationFields , false),
    DEFINE_DETAIL(QContactPhoneNumber   , PhoneNumbers   , phoneNumberFields  , true),
    DEFINE_DETAIL(QContactPresence      , Presences      , presenceFields     , false),
    DEFINE_DETAIL(QContactRingtone      , Ringtones      , ringtoneFields     , false),
    DEFINE_DETAIL(QContactTag           , Tags           , tagFields          , true),
    DEFINE_DETAIL(QContactUrl           , Urls           , urlFields          , true),
    DEFINE_DETAIL(QContactTpMetadata    , TpMetadata     , tpMetadataFields   , false),
    DEFINE_DETAIL(QContactGlobalPresence, GlobalPresences, presenceFields     , true)
};

static QString buildWhere(const QContactDetailFilter &filter, QVariantList *bindings, bool *failed)
{
    if (filter.matchFlags() & QContactFilter::MatchKeypadCollation) {
        *failed = true;
        qWarning() << "Cannot buildWhere with filter requiring keypad collation";
        return QLatin1String("FAILED");
    }

    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (detail.detail != filter.detailDefinitionName())
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];

            if (!filter.detailFieldName().isEmpty() && field.field != filter.detailFieldName())
                continue;

            if (filter.detailFieldName().isEmpty()   // "match if detail exists, don't care about field or value" filter
                    || !filter.value().isValid()     // "match if detail and field exists, don't care about value" filter
                    || (filter.detailDefinitionName() == QContactSyncTarget::DefinitionName
                            && filter.detailFieldName() == QContactSyncTarget::FieldSyncTarget
                            && filter.value().toString().isEmpty())) { // match all sync targets if empty sync target filter
                const QString comparison(QLatin1String("%1 IS NOT NULL"));
                return detail.where().arg(comparison.arg(field.column));
            }

            bool stringField = field.fieldType == StringField;
            bool phoneNumberMatch = filter.matchFlags() & QContactFilter::MatchPhoneNumber;
            bool caseSensitive = filter.matchFlags() & QContactFilter::MatchCaseSensitive;
            bool useNormalizedNumber = false;
            int globValue = filter.matchFlags() & 7;

            QString comparison = (stringField && !caseSensitive) ? QLatin1String("lower(%1)") : QLatin1String("%1");
            QString bindValue;
            QString column;

            if (phoneNumberMatch) {
                // If the phone number match is on the number field of a phoneNumber detail, then
                // match on the normalized number rather than the unconstrained number (for simple matches)
                useNormalizedNumber = (filter.detailDefinitionName() == QContactPhoneNumber::DefinitionName &&
                                       filter.detailFieldName() == QContactPhoneNumber::FieldNumber &&
                                       globValue != QContactFilter::MatchStartsWith &&
                                       globValue != QContactFilter::MatchContains &&
                                       globValue != QContactFilter::MatchEndsWith);

                if (useNormalizedNumber) {
                    // Normalize the input for comparison
                    bindValue = ContactsEngine::normalizedPhoneNumber(filter.value().toString());
                    if (!caseSensitive) {
                        bindValue = bindValue.toLower();
                    }
                    column = QString::fromLatin1("NormalizedNumber");
                } else {
                    // remove any non-digit characters from the column value when we do our comparison: +,-, ,#,(,) are removed.
                    comparison = QLatin1String("replace(replace(replace(replace(replace(replace(%1, '+', ''), '-', ''), '#', ''), '(', ''), ')', ''), ' ', '')");
                    QString tempValue = caseSensitive ? filter.value().toString() : filter.value().toString().toLower();
                    for (int i = 0; i < tempValue.size(); ++i) {
                        QChar current = tempValue.at(i).toLower();
                        if (current.isDigit()) {
                            bindValue.append(current);
                        }
                    }
                }
            } else {
                bindValue = caseSensitive ? filter.value().toString() : filter.value().toString().toLower();
            }

            if (stringField && (globValue == QContactFilter::MatchStartsWith)) {
                bindValue = bindValue + QLatin1String("*");
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else if (stringField && (globValue == QContactFilter::MatchContains)) {
                bindValue = QLatin1String("*") + bindValue + QLatin1String("*");
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else if (stringField && (globValue == QContactFilter::MatchEndsWith)) {
                bindValue = QLatin1String("*") + bindValue;
                comparison += QLatin1String(" GLOB ?");
                bindings->append(bindValue);
            } else {
                if (phoneNumberMatch && !useNormalizedNumber) {
                    bindValue = QLatin1String("*") + bindValue;
                    comparison += QLatin1String(" GLOB ?");
                    bindings->append(bindValue);
                } else {
                    comparison += (stringField && !caseSensitive) ? QLatin1String(" = lower(?)") : QLatin1String(" = ?");
                    bindings->append(bindValue);
                }
            }

            return detail.where().arg(comparison.arg(column.isEmpty() ? field.column : column));
        }
    }

    *failed = true;
    qWarning() << "Cannot buildWhere with unknown DetailFilter detail:" << filter.detailDefinitionName();
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactDetailRangeFilter &filter, QVariantList *bindings, bool *failed)
{
    for (int i = 0; i < lengthOf(detailInfo); ++i) {
        const DetailInfo &detail = detailInfo[i];
        if (detail.detail != filter.detailDefinitionName())
            continue;

        for (int j = 0; j < detail.fieldCount; ++j) {
            const FieldInfo &field = detail.fields[j];

            if (!filter.detailFieldName().isEmpty() && field.field != filter.detailFieldName())
                continue;

            if (filter.detailFieldName().isEmpty()
                    || (!filter.minValue().isValid() && !filter.maxValue().isValid())) {
                // "match if detail exists, don't care about field or value" filter
                const QString comparison(QLatin1String("%1 IS NOT NULL"));
                return detail.where().arg(comparison.arg(field.column));
            }

            QString comparison;
            bool stringField = field.fieldType == StringField;
            bool caseSensitive = filter.matchFlags() & QContactFilter::MatchCaseSensitive;
            bool needsAnd = false;
            if (filter.minValue().isValid()) {
                bindings->append(filter.minValue());
                if (stringField && !caseSensitive) {
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
                bindings->append(filter.maxValue());
                if (stringField && !caseSensitive) {
                    comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                            ? QString(QLatin1String("%1 <= lower(?)"))
                            : QString(QLatin1String("%1 < lower(?)"));
                } else {
                    comparison += (filter.rangeFlags() & QContactDetailRangeFilter::IncludeUpper)
                            ? QString(QLatin1String("%1 <= ?"))
                            : QString(QLatin1String("%1 < ?"));
                }
                
            }

            QString comparisonArg = (stringField && !caseSensitive) ? QString(QLatin1String("lower(%1)")).arg(field.column) : field.column;
            return detail.where().arg(comparison.arg(comparisonArg));
        }
    }

    *failed = true;
    qWarning() << "Cannot buildWhere with unknown DetailRangeFilter detail:" << filter.detailDefinitionName();
    return QLatin1String("FALSE");
}

static QString buildWhere(const QContactLocalIdFilter &filter, QVariantList *bindings, bool *failed)
{
    const QList<QContactLocalId> contactIds = filter.ids();
    if (contactIds.isEmpty()) {
        *failed = true;
        qWarning() << "Cannot buildWhere with empty contact ID list";
        return QLatin1String("FALSE");
    }

    QString statement = QLatin1String("Contacts.contactId IN (?");
    bindings->append(contactIds.first() - 1);

    for (int i = 1; i < contactIds.count(); ++i) {
        statement += QLatin1String(",?");
        bindings->append(contactIds.at(i) - 1);
    }
    return statement + QLatin1String(")");
}

static QString buildWhere(const QContactRelationshipFilter &filter, QVariantList *bindings, bool *failed)
{
    QContactId rci = filter.relatedContactId();
    QContactRelationship::Role rcr = filter.relatedContactRole();
    QString rt = filter.relationshipType();
    QContactLocalId rclid = rci.localId();

    if (!rci.managerUri().isEmpty() && rci.managerUri() != QLatin1String("org.nemomobile.contacts.sqlite")) {
        *failed = true;
        qWarning() << "Cannot buildWhere with invalid manager URI:" << rci.managerUri();
        return QLatin1String("FALSE");
    }

    bool needsId = rclid != 0;
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
            bindings->append(rclid - 1);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ?)");
            bindings->append(rclid - 1);
        } else { // where the specified contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE firstId = ?)");
            bindings->append(rclid - 1);
            bindings->append(rclid - 1);
        }
    } else if (needsId && needsType) {
        // return the id of every contact who is in a relationship of the specified type with the specified contact
        if (rcr == QContactRelationship::First) { // where the specified contact is the First
            statement += QLatin1String("SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?)");
            bindings->append(rclid - 1);
            bindings->append(rt);
        } else if (rcr == QContactRelationship::Second) { // where the specified contact is the Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ?)");
            bindings->append(rclid - 1);
            bindings->append(rt);
        } else { // where the specified contact is either First or Second
            statement += QLatin1String("SELECT DISTINCT firstId FROM Relationships WHERE secondId = ? AND type = ? UNION SELECT DISTINCT secondId FROM Relationships WHERE firstId = ? AND type = ?)");
            bindings->append(rclid - 1);
            bindings->append(rt);
            bindings->append(rclid - 1);
            bindings->append(rt);
        }
    }

    return statement;
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
    case QContactFilter::RelationshipFilter:
        return buildWhere(static_cast<const QContactRelationshipFilter &>(filter), bindings, failed);
    case QContactFilter::IntersectionFilter:
        return buildWhere(static_cast<const QContactIntersectionFilter &>(filter), bindings, failed);
    case QContactFilter::UnionFilter:
        return buildWhere(static_cast<const QContactUnionFilter &>(filter), bindings, failed);
    case QContactFilter::LocalIdFilter:
        return buildWhere(static_cast<const QContactLocalIdFilter &>(filter), bindings, failed);
    default:
        *failed = true;
        qWarning() << "Cannot buildWhere with unknown filter type" << filter.type();
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
                qWarning() << "UNSUPPORTED SORTING: no join and not primary table for ORDER BY in query with:"
                           << order.detailDefinitionName() << order.detailFieldName();
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

static QContactManager::Error createTemporaryContactIdsTable(
        QSqlDatabase *db, const QString &table, bool filter, // required for both "filter" and "by id"
        const QVariantList &boundIds,                        // for "read contacts by id" only
        const QString &join, const QString &where, const QString &orderBy, const QVariantList boundValues)
{
    // Create the temporary table (if we haven't already).
    QSqlQuery tableQuery(*db);
    const QString createStatement = QString(QLatin1String(
            "\n CREATE TABLE IF NOT EXISTS temp.%1 ("
            "\n contactId INTEGER);")).arg(table);
    if (!tableQuery.prepare(createStatement)) {
        qWarning() << "Failed to prepare temporary table query";
        qWarning() << tableQuery.lastError();
        qWarning() << createStatement;
        return QContactManager::UnspecifiedError;
    }
    if (!tableQuery.exec()) {
        qWarning() << "Failed to create temporary table";
        qWarning() << tableQuery.lastError();
        qWarning() << createStatement;
        return QContactManager::UnspecifiedError;
    }
    tableQuery.finish();

    // Delete all existing records.  This is just in case the
    // previous clearTemporaryContactIdsTable function failed.
    // XXX TODO: for performance reasons, should remove this query.
    QSqlQuery deleteRecordsQuery(*db);
    const QString deleteRecordsStatement = QString(QLatin1String(
            "\n DELETE FROM temp.%1")).arg(table);
    if (!deleteRecordsQuery.prepare(deleteRecordsStatement)) {
        qWarning() << "Failed to prepare delete records query";
        qWarning() << deleteRecordsQuery.lastError();
        qWarning() << deleteRecordsStatement;
        return QContactManager::UnspecifiedError;
    }
    if (!deleteRecordsQuery.exec()) {
        qWarning() << "Failed to delete temporary records";
        qWarning() << deleteRecordsQuery.lastError();
        qWarning() << deleteRecordsStatement;
        return QContactManager::UnspecifiedError;
    }
    deleteRecordsQuery.finish();

    // insert into the temporary table, all of the ids
    // which will be specified either by id list, or by filter.
    QSqlQuery insertQuery(*db);
    if (filter) {
        // specified by filter
        const QString insertStatement = QString(QLatin1String(
                "\n INSERT INTO temp.%1 (contactId)"
                "\n SELECT Contacts.contactId"
                "\n FROM Contacts %2"
                "\n %3"
                "\n ORDER BY %4;"))
                .arg(table).arg(join).arg(where).arg(orderBy);
        if (!insertQuery.prepare(insertStatement)) {
            qWarning() << "Failed to prepare temporary contact ids";
            qWarning() << insertQuery.lastError();
            qWarning() << insertStatement;
            return QContactManager::UnspecifiedError;
        }
        for (int i = 0; i < boundValues.count(); ++i) {
            insertQuery.bindValue(i, boundValues.at(i));
        }
        if (!insertQuery.exec()) {
            qWarning() << "Failed to insert temporary contact ids";
            qWarning() << insertQuery.lastError();
            qWarning() << insertStatement;
            return QContactManager::UnspecifiedError;
        }
    } else {
        // specified by id list
        const QString insertStatement = QString(QLatin1String(
                "\n INSERT INTO temp.%1 (contactId)"
                "\n VALUES(:contactId);"))
                .arg(table);
        if (!insertQuery.prepare(insertStatement)) {
            qWarning() << "Failed to prepare temporary contact ids";
            qWarning() << insertQuery.lastError();
            qWarning() << insertStatement;
            return QContactManager::UnspecifiedError;
        }
        insertQuery.bindValue(0, boundIds);
        if (!insertQuery.execBatch()) {
            qWarning() << "Failed to insert temporary contact ids";
            qWarning() << insertQuery.lastError();
            qWarning() << insertStatement;
            return QContactManager::UnspecifiedError;
        }
    }
    insertQuery.finish();
    return QContactManager::NoError;
}

static void clearTemporaryContactIdsTable(QSqlDatabase *db, const QString &table)
{
    QSqlQuery dropTableQuery(*db);
    const QString dropTableStatement = QString(QLatin1String(
            "\n DROP TABLE temp.%1")).arg(table);
    if (!dropTableQuery.prepare(dropTableStatement) || !dropTableQuery.exec()) {
        // couldn't drop the table, just delete all entries instead.
        QSqlQuery deleteRecordsQuery(*db);
        const QString deleteRecordsStatement = QString(QLatin1String(
                "\n DELETE FROM temp.%1")).arg(table);
        if (!deleteRecordsQuery.prepare(deleteRecordsStatement)) {
            qWarning() << "FATAL ERROR: Failed to prepare delete records query - the next query may return spurious results";
            qWarning() << deleteRecordsQuery.lastError();
            qWarning() << deleteRecordsStatement;
        }
        if (!deleteRecordsQuery.exec()) {
            qWarning() << "FATAL ERROR: Failed to delete temporary records - the next query may return spurious results";
            qWarning() << deleteRecordsQuery.lastError();
            qWarning() << deleteRecordsStatement;
        }
    }
}

namespace {

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
bool includesSelfId(const QContactLocalIdFilter &filter)
{
    static const QContactLocalId selfLocalId(2 + 1);
    return filter.ids().contains(selfLocalId);
}
bool includesSelfId(const QContactFilter &filter)
{
    switch (filter.type()) {
    case QContactFilter::DefaultFilter:
    case QContactFilter::ContactDetailFilter:
    case QContactFilter::ContactDetailRangeFilter:
    case QContactFilter::RelationshipFilter:
        return false;

    case QContactFilter::IntersectionFilter:
        return includesSelfId(static_cast<const QContactIntersectionFilter &>(filter));
    case QContactFilter::UnionFilter:
        return includesSelfId(static_cast<const QContactUnionFilter &>(filter));
    case QContactFilter::LocalIdFilter:
        return includesSelfId(static_cast<const QContactLocalIdFilter &>(filter));

    default:
        qWarning() << "Cannot includesSelfId with unknown filter type" << filter.type();
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
        if (!includesSelfContactId(filter)) {
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
        const QStringList &details)
{
    QString join;
    const QString orderBy = buildOrderBy(order, &join);
    bool whereFailed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, &bindings, &whereFailed);
    if (whereFailed) {
        qWarning() << "Failed to create WHERE expression: invalid filter specification";
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter);

    QContactManager::Error createTempError = createTemporaryContactIdsTable(
            &m_database, table, true, QVariantList(), join, where, orderBy, bindings);

    QContactManager::Error error = (createTempError == QContactManager::NoError)
            ? queryContacts(table, contacts, details)
            : createTempError;

    clearTemporaryContactIdsTable(&m_database, table);

    return error;
}

QContactManager::Error ContactReader::readContacts(
        const QString &table,
        QList<QContact> *contacts,
        const QList<QContactLocalId> &contactIds,
        const QStringList &details)
{
    // XXX TODO: get rid of this query, just iterate over the returned results in memory
    // and if the id doesn't match, insert an empty contact (and error) for that index.
    QList<QContactLocalId> existingIds;
    QSqlQuery queryExistingIds(m_database);
    if (!queryExistingIds.exec("SELECT DISTINCT contactId FROM Contacts")) {
        qWarning() << "Failed to query existing contacts";
        qWarning() << queryExistingIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (queryExistingIds.next()) {
        existingIds.append(queryExistingIds.value(0).toUInt() + 1);
    }
    queryExistingIds.finish();

    QList<int> zeroIndices;
    QVariantList boundIds;
    for (int i = 0; i < contactIds.size(); ++i) {
        QContactLocalId currLId = contactIds.at(i);
        if (currLId == 0 || !existingIds.contains(currLId)) {
            zeroIndices.append(i);
        } else {
            boundIds.append(currLId - 1);
        }
    }

    QContactManager::Error createTempError = createTemporaryContactIdsTable(
            &m_database, table, false, boundIds, QString(), QString(), QString(), QVariantList());

    QContactManager::Error error = (createTempError == QContactManager::NoError)
            ? queryContacts(table, contacts, details)
            : createTempError;

    clearTemporaryContactIdsTable(&m_database, table);

    for (int i = 0; i < zeroIndices.size(); ++i) {
        contacts->insert(zeroIndices.at(i), QContact());
        error = QContactManager::DoesNotExistError;
    }
    if (contacts && (contacts->size() != contactIds.size())) {
        error = QContactManager::DoesNotExistError;
    }

    return error;
}

QContactManager::Error ContactReader::queryContacts(
        const QString &table, QList<QContact> *contacts, const QStringList &details)
{
    QSqlQuery query(m_database);
    if (!query.exec(QString(QLatin1String(
            "\n SELECT Contacts.*"
            "\n FROM temp.%1 INNER JOIN Contacts ON temp.%1.contactId = Contacts.contactId;")).arg(table))) {
        qWarning() << "Failed to query from" << table;
        qWarning() << query.lastError();
        return QContactManager::UnspecifiedError;
    }

    const QString tableTemplate = QString(QLatin1String(
            "\n SELECT"
            "\n  Details.detailUri,"
            "\n  Details.linkedDetailUris,"
            "\n  Details.contexts,"
            "\n  Details.accessConstraints,"
            "\n  %2.*"
            "\n FROM temp.%1"
            "\n  INNER JOIN %2 ON temp.%1.contactId = %2.contactId"
            "\n  LEFT JOIN Details ON %2.detailId = Details.detailId AND Details.detail = :detail;")).arg(table);

    QList<Table> tables;
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

            const QString tableQueryStatement(tableTemplate.arg(QLatin1String(detail.table)));
            if (!table.query.prepare(tableQueryStatement)) {
                qWarning() << "Failed to prepare table" << detail.table;
                qWarning() << tableQueryStatement;
                qWarning() << table.query.lastError();
            } else {
                table.query.bindValue(0, detail.detail);
                if (!table.query.exec()) {
                    qWarning() << "Failed to query table" << detail.table;
                    qWarning() << table.query.lastError();
                } else if (table.query.next()) {
                    table.currentId = table.query.value(5).toUInt();
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

            QString persistedDL = query.value(1).toString();
            if (!persistedDL.isEmpty())
                QContactManagerEngine::setContactDisplayLabel(&contact, persistedDL);

            QContactName name;
            setValue(&name, QContactName::FieldFirstName  , query.value(2));
            setValue(&name, QContactName::FieldLastName   , query.value(3));
            setValue(&name, QContactName::FieldMiddleName , query.value(4));
            setValue(&name, QContactName::FieldPrefix     , query.value(5));
            setValue(&name, QContactName::FieldSuffix     , query.value(6));
            setValue(&name, QContactName::FieldCustomLabel, query.value(7));
            if (!name.isEmpty())
                contact.saveDetail(&name);

            QContactSyncTarget starget;
            setValue(&starget, QContactSyncTarget::FieldSyncTarget, query.value(8));
            if (!starget.isEmpty())
                contact.saveDetail(&starget);

            QContactTimestamp timestamp;
            setValue(&timestamp, QContactTimestamp::FieldCreationTimestamp    , query.value(9));
            setValue(&timestamp, QContactTimestamp::FieldModificationTimestamp, query.value(10));
            if (!timestamp.isEmpty())
                contact.saveDetail(&timestamp);

            QContactGender gender;
            setValue(&gender, QContactGender::FieldGender, query.value(11));
            if (!gender.isEmpty())
                contact.saveDetail(&gender);

            QContactFavorite favorite;
            setValue(&favorite, QContactFavorite::FieldFavorite, query.value(12));
            if (!favorite.isEmpty())
                contact.saveDetail(&favorite);

            for (int j = 0; j < tables.count(); ++j) {
                Table &table = tables[j];
                if (table.query.isValid() && table.currentId == contactId) {
                    table.read(contactId, &contact, &table.query, table.currentId);
                }
            }

            // XXX TODO: fetch hint - if "don't fetch relationships" is specified, skip this!
            QList<QContactRelationship> currContactRelationships;
            QList<QContactRelationship> ccfrels;
            QList<QContactRelationship> ccsrels;
            readRelationships(&ccfrels, QString(), id, QContactId());
            readRelationships(&ccsrels, QString(), QContactId(), id);
            currContactRelationships << ccfrels << ccsrels;
            QContactManagerEngine::setContactRelationships(&contact, currContactRelationships);

            contacts->append(contact);
        }
        contactsAvailable(*contacts);
    } while (query.isValid());

    query.finish();
    for (int k = 0; k < tables.count(); ++k) {
        Table &table = tables[k];
        table.query.finish();
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactReader::readContactIds(
        QList<QContactLocalId> *contactIds,
        const QContactFilter &filter,
        const QList<QContactSortOrder> &order)
{
    QString join;
    const QString orderBy = buildOrderBy(order, &join);
    bool failed = false;
    QVariantList bindings;
    QString where = buildWhere(filter, &bindings, &failed);

    if (failed) {
        qWarning() << "Failed to create WHERE expression: invalid filter specification";
        return QContactManager::UnspecifiedError;
    }

    where = expandWhere(where, filter);

    const QString queryString = QString(QLatin1String(
                "\n SELECT DISTINCT Contacts.contactId"
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
    if (identity == ContactsDatabase::SelfContactId) {
        // we don't allow setting the self contact id, it's always static
        *contactId = 2 + 1; // 2 is the database id, +1 to turn into contact id.
    } else {
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
    }

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
            "\n FROM Relationships") + where + QLatin1String(";");

    QSqlQuery query(m_database);
    if (!query.prepare(statement)) {
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
        firstId.setManagerUri(QLatin1String("org.nemomobile.contacts.sqlite"));
        firstId.setLocalId(query.value(1).toUInt() + 1);
        relationship.setFirst(firstId);

        QContactId secondId;
        secondId.setManagerUri(QLatin1String("org.nemomobile.contacts.sqlite"));
        secondId.setLocalId(query.value(2).toUInt() + 1);
        relationship.setSecond(secondId);

        relationships->append(relationship);
    }
    query.finish();

    return QContactManager::NoError;
}

void ContactReader::contactsAvailable(const QList<QContact> &)
{
}

void ContactReader::contactIdsAvailable(const QList<QContactLocalId> &)
{
}
