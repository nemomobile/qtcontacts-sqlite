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

#ifndef QTCONTACTS_EXTENSIONS_H
#define QTCONTACTS_EXTENSIONS_H

#include "qtcontacts-extensions-config.h"

#include <QContactDetail>
#include <QContactId>

#ifdef USING_QTPIM
#include <QContactOnlineAccount>
#include <QContactPhoneNumber>
#include <QContactAvatar>
#include <QContactName>
#endif

// Defines the extended values supported by qtcontacts-sqlite

#ifdef USING_QTPIM
QT_BEGIN_NAMESPACE_CONTACTS
#elif defined(USING_QTMOBILITY)
QTM_BEGIN_NAMESPACE
#else
#error "QtContacts variant in use is not specified"
#endif

#ifdef USING_QTPIM
// In QContactDetail::contexts(), we support additional values, "Default" and "Large"
static const int QContactDetail__ContextDefault = (QContactDetail::ContextOther+1);
static const int QContactDetail__ContextLarge = (QContactDetail::ContextOther+2);

// In QContactName, we support the customLabel property
static const int QContactName__FieldCustomLabel = (QContactName::FieldSuffix+1);

// In QContactOnlineAccount we support the following properties:
//   AccountPath - identifying path value for the account
//   AccountIconPath - path to an icon indicating the service type of the account
//   Enabled - a boolean indicating whether or not the account is enabled for activity
static const int QContactOnlineAccount__FieldAccountPath = (QContactOnlineAccount::FieldSubTypes+1);
static const int QContactOnlineAccount__FieldAccountIconPath = (QContactOnlineAccount::FieldSubTypes+2);
static const int QContactOnlineAccount__FieldEnabled = (QContactOnlineAccount::FieldSubTypes+3);

// In QContactPhoneNumber, we support a field for normalized form of the number
static const int QContactPhoneNumber__FieldNormalizedNumber = (QContactPhoneNumber::FieldSubTypes+1);

// In QContactAvatar, we support a field for storing caller metadata
static const int QContactAvatar__FieldAvatarMetadata = (QContactAvatar::FieldVideoUrl+1);

// We support the QContactOriginMetadata detail type
static const QContactDetail::DetailType QContactDetail__TypeOriginMetadata = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 1);

// We support the QContactStatusFlags detail type
static const QContactDetail::DetailType QContactDetail__TypeStatusFlags = static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 2);

#else
// Declared as static:
Q_DECLARE_LATIN1_CONSTANT(QContactDetail__ContextDefault, "Default") = { "Default" };
Q_DECLARE_LATIN1_CONSTANT(QContactDetail__ContextLarge, "Large") = { "Large" };

Q_DECLARE_LATIN1_CONSTANT(QContactOnlineAccount__FieldAccountPath, "AccountPath") = { "AccountPath" };
Q_DECLARE_LATIN1_CONSTANT(QContactOnlineAccount__FieldAccountIconPath, "AccountIconPath") = { "AccountIconPath" };
Q_DECLARE_LATIN1_CONSTANT(QContactOnlineAccount__FieldEnabled, "Enabled") = { "Enabled" };

Q_DECLARE_LATIN1_CONSTANT(QContactPhoneNumber__FieldNormalizedNumber, "NormalizedNumber") = { "NormalizedNumber" };

Q_DECLARE_LATIN1_CONSTANT(QContactAvatar__FieldAvatarMetadata, "AvatarMetadata") = { "AvatarMetadata" };
#endif

#ifdef USING_QTPIM
QT_END_NAMESPACE_CONTACTS
#else
QTM_END_NAMESPACE
#endif

namespace QtContactsSqliteExtensions {

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#else
QTM_USE_NAMESPACE
#endif

#ifdef USING_QTPIM
typedef QContactId ApiContactIdType;
#else
typedef QContactLocalId ApiContactIdType;
#endif

ApiContactIdType apiContactId(quint32);
quint32 internalContactId(const ApiContactIdType &);
#ifndef USING_QTPIM
quint32 internalContactId(const QContactId &);
#endif

enum NormalizePhoneNumberFlags {
    KeepPhoneNumberPunctuation = (1 << 0),
    KeepPhoneNumberDialString = (1 << 1),
    ValidatePhoneNumber = (1 << 2)
};

QString normalizePhoneNumber(const QString &input, NormalizePhoneNumberFlags flags);
QString minimizePhoneNumber(const QString &input, int maxCharacters = 7);

}

#endif
