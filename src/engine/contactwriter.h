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

#include "contactsdatabase.h"
#include "contactid_p.h"

#include "../extensions/qtcontacts-extensions.h"
#include "../extensions/qcontactoriginmetadata.h"
#include "../extensions/contactmanagerengine.h"

#include <QContactAddress>
#include <QContactAnniversary>
#include <QContactAvatar>
#include <QContactBirthday>
#include <QContactEmailAddress>
#include <QContactExtendedDetail>
#include <QContactGlobalPresence>
#include <QContactGuid>
#include <QContactHobby>
#include <QContactNickname>
#include <QContactNote>
#include <QContactOnlineAccount>
#include <QContactOrganization>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactRingtone>
#include <QContactTag>
#include <QContactUrl>
#include <QContactManager>

#include <QSet>
#include <QSqlQuery>

QTCONTACTS_USE_NAMESPACE

class ProcessMutex;
class ContactsEngine;
class ContactReader;
class ContactWriter
{
public:
    typedef QList<QContactDetail::DetailType> DetailList;

    ContactWriter(const ContactsEngine &engine, const QSqlDatabase &database, ContactReader *reader);
    ~ContactWriter();

    QContactManager::Error save(
            QList<QContact> *contacts,
            const DetailList &definitionMask,
            QMap<int, bool> *aggregateUpdated,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate,
            bool withinSyncUpdate);
    QContactManager::Error remove(const QList<QContactId> &contactIds,
                                  QMap<int, QContactManager::Error> *errorMap,
                                  bool withinTransaction);

    QContactManager::Error setIdentity(ContactsDatabase::Identity identity, QContactId contactId);

    QContactManager::Error save(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction);
    QContactManager::Error remove(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    QContactManager::Error fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                             QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds);

    QContactManager::Error updateSyncContacts(const QString &syncTarget,
                                              QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                              const QList<QPair<QContact, QContact> > &remoteChanges);
#endif

    bool storeOOB(const QString &scope, const QMap<QString, QVariant> &values);
    bool removeOOB(const QString &scope, const QStringList &keys);

private:
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();

    QContactManager::Error create(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinAggregateUpdate);
    QContactManager::Error update(QContact *contact, const DetailList &definitionMask, bool *aggregateUpdated, bool withinTransaction, bool withinAggregateUpdate);
    QContactManager::Error write(quint32 contactId, QContact *contact, const DetailList &definitionMask);

    QContactManager::Error saveRelationships(const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap);
    QContactManager::Error removeRelationships(const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    QContactManager::Error setAggregate(QContact *contact, quint32 contactId, bool update, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error calculateDelta(QContact *contact, const ContactWriter::DetailList &definitionMask,
                                          QList<QContactDetail> *addDelta, QList<QContactDetail> *removeDelta, QList<QContact> *writeList);
    QContactManager::Error updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction, quint32 *aggregateContactId = 0);
    QContactManager::Error updateLocalAndAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error regenerateAggregates(const QList<quint32> &aggregateIds, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error removeChildlessAggregates(QList<QContactId> *realRemoveIds);
    QContactManager::Error aggregateOrphanedContacts(bool withinTransaction);

    QContactManager::Error syncFetch(const QString &syncTarget, const QDateTime &lastSync, const QSet<quint32> &exportedIds,
                                     QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds);

    QContactManager::Error syncUpdate(const QString &syncTarget,
                                      QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                      const QList<QPair<QContact, QContact> > &remoteChanges);
#endif

    void bindContactDetails(const QContact &contact, QSqlQuery &query, const DetailList &definitionMask = DetailList(), quint32 contactId = 0);

    template <typename T> bool writeDetails(
            quint32 contactId,
            QContact *contact,
            QSqlQuery &removeQuery,
            const DetailList &definitionMask,
            const QString &syncTarget,
            bool syncable,
            bool wasLocal,
            QContactManager::Error *error);

    template <typename T> bool writeCommonDetails(
            quint32 contactId,
            const QVariant &detailId,
            const T &detail,
            bool syncable,
            bool wasLocal,
            QContactManager::Error *error);

    template <typename T> bool removeCommonDetails(quint32 contactId, QContactManager::Error *error);

    QSqlQuery &bindDetail(quint32 contactId, const QContactAddress &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactAnniversary &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactAvatar &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactBirthday &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactEmailAddress &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactGlobalPresence &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactGuid &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactHobby &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactNickname &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactNote &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactOnlineAccount &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactOrganization &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactPhoneNumber &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactPresence &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactRingtone &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactTag &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactUrl &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactOriginMetadata &detail);
    QSqlQuery &bindDetail(quint32 contactId, const QContactExtendedDetail &detail);

    const ContactsEngine &m_engine;
    QSqlDatabase m_database;
    ProcessMutex *m_databaseMutex;
    QSqlQuery m_findConstituentsForAggregate;
    QSqlQuery m_findLocalForAggregate;
    QSqlQuery m_findAggregateForContact;
    QSqlQuery m_childlessAggregateIds;
    QSqlQuery m_orphanContactIds;
    QSqlQuery m_countLocalConstituents;
    QSqlQuery m_updateSyncTarget;
    QSqlQuery m_checkContactExists;
    QSqlQuery m_existingContactIds;
    QSqlQuery m_modifiableDetails;
    QSqlQuery m_selfContactId;
    QSqlQuery m_syncContactIds;
    QSqlQuery m_aggregateContactIds;
    QSqlQuery m_constituentContactDetails;
    QSqlQuery m_heuristicallyMatchData;
    QSqlQuery m_localConstituentIds;
    QSqlQuery m_affectedSyncTargets;
    QSqlQuery m_addedSyncContactIds;
    QSqlQuery m_deletedSyncContactIds;
    QSqlQuery m_insertContact;
    QSqlQuery m_updateContact;
    QSqlQuery m_removeContact;
    QSqlQuery m_existingRelationships;
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
    QSqlQuery m_insertTag;
    QSqlQuery m_insertUrl;
    QSqlQuery m_insertOriginMetadata;
    QSqlQuery m_insertExtendedDetail;
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
    QSqlQuery m_removeTag;
    QSqlQuery m_removeUrl;
    QSqlQuery m_removeOriginMetadata;
    QSqlQuery m_removeExtendedDetail;
    QSqlQuery m_removeDetail;
    QSqlQuery m_removeIdentity;
    QSqlQuery m_findConstituentsForAggregateIds;
    QSqlQuery m_findAggregateForContactIds;
    QSqlQuery m_selectAggregateContactIds;
    ContactReader *m_reader;

    QSet<QContactId> m_addedIds;
    QSet<QContactId> m_removedIds;
    QSet<QContactId> m_changedIds;
    QSet<QContactId> m_presenceChangedIds;
    QSet<QString> m_changedSyncTargets;
    QSet<quint32> m_changedLocalIds;
    QSet<QString> m_suppressedSyncTargets;
};


#endif
