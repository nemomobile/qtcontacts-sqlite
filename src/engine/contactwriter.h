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

#ifndef QTCONTACTSSQLITE_CONTACTWRITER
#define QTCONTACTSSQLITE_CONTACTWRITER

#include <QSqlQuery>

#include <QContactAddress>
#include <QContactAnniversary>
#include <QContactAvatar>
#include <QContactBirthday>
#include <QContactEmailAddress>
#include <QContactGuid>
#include <QContactHobby>
#include <QContactNickname>
#include <QContactNote>
#include <QContactOnlineAccount>
#include <QContactOrganization>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactRingtone>
#include <QContactSyncTarget>
#include <QContactTag>
#include <QContactUrl>

#include <QContactManager>

#include "contactsdatabase.h"

QTM_USE_NAMESPACE


class ContactWriter
{
public:
    ContactWriter(const QSqlDatabase &database);
    ~ContactWriter();

    QContactManager::Error save(
            QList<QContact> *contacts,
            const QStringList &definitionMask,
            QMap<int, QContactManager::Error> *errorMap);
    QContactManager::Error remove(const QList<QContactLocalId> &contactIds);

    QContactManager::Error setIdentity(ContactsDatabase::Identity identity, QContactLocalId contactId);

    QContactManager::Error save(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap);
    QContactManager::Error remove(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap);

private:
    QContactManager::Error create(QContact *contact, const QStringList &definitionMask);
    QContactManager::Error update(QContact *contact, const QStringList &definitionMask);
    QContactManager::Error write(
            QContactLocalId contactId, QContact *contact, const QStringList &definitionMask);
    void bindContactDetails(const QContact &contact, QSqlQuery &query);

    template <typename T> bool writeDetails(
            QContactLocalId contactId,
            QContact *contact,
            QSqlQuery &removeQuery,
            const QStringList &definitionMask,
            QContactManager::Error *error);

    template <typename T> bool writeCommonDetails(
                QContactLocalId contactId, const QSqlQuery &query, const T &detail, QContactManager::Error *error);
    template <typename T> bool removeCommonDetails(
                QContactLocalId contactId, QContactManager::Error *error);

    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactAddress &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactAnniversary &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactAvatar &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactBirthday &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactEmailAddress &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactGuid &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactHobby &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactNickname &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactNote &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactOnlineAccount &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactOrganization &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactPhoneNumber &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactPresence &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactRingtone &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactSyncTarget &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactTag &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactUrl &detail);
    QSqlQuery &bindDetail(QContactLocalId contactId, const QContactTpMetadata &detail);

    QSqlDatabase m_database;
    QSqlQuery m_checkContactExists;
    QSqlQuery m_insertContact;
    QSqlQuery m_updateContact;
    QSqlQuery m_removeContact;
    QSqlQuery m_insertRelationship;
    QSqlQuery m_removeRelationship;
    QSqlQuery m_insertAddress;
    QSqlQuery m_insertAnniversary;
    QSqlQuery m_insertAvatar;
    QSqlQuery m_insertBirthday;
    QSqlQuery m_insertEmailAddress;
    QSqlQuery m_insertGlobalPresence;
    QSqlQuery m_insertGuid;
    QSqlQuery m_insertHobby;
    QSqlQuery m_insertNickname;
    QSqlQuery m_insertNote;
    QSqlQuery m_insertOnlineAccount;
    QSqlQuery m_insertOrganization;
    QSqlQuery m_insertPhoneNumber;
    QSqlQuery m_insertPresence;
    QSqlQuery m_insertRingtone;
    QSqlQuery m_insertSyncTarget;
    QSqlQuery m_insertTag;
    QSqlQuery m_insertUrl;
    QSqlQuery m_insertTpMetadata;
    QSqlQuery m_insertDetail;
    QSqlQuery m_insertIdentity;
    QSqlQuery m_removeAddress;
    QSqlQuery m_removeAnniversary;
    QSqlQuery m_removeAvatar;
    QSqlQuery m_removeBirthday;
    QSqlQuery m_removeEmailAddress;
    QSqlQuery m_removeGlobalPresence;
    QSqlQuery m_removeGuid;
    QSqlQuery m_removeHobby;
    QSqlQuery m_removeNickname;
    QSqlQuery m_removeNote;
    QSqlQuery m_removeOnlineAccount;
    QSqlQuery m_removeOrganization;
    QSqlQuery m_removePhoneNumber;
    QSqlQuery m_removePresence;
    QSqlQuery m_removeRingtone;
    QSqlQuery m_removeSyncTarget;
    QSqlQuery m_removeTag;
    QSqlQuery m_removeUrl;
    QSqlQuery m_removeTpMetadata;
    QSqlQuery m_removeDetail;
    QSqlQuery m_removeIdentity;
};


#endif
