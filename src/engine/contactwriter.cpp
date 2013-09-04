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

#include "contactwriter.h"

#include "contactsengine.h"
#include "contactreader.h"
#include "contactnotifier.h"
#include "conversion_p.h"
#include "semaphore_p.h"

#include <QContactStatusFlags>

#include <QContactFavorite>
#include <QContactGender>
#include <QContactGlobalPresence>
#include <QContactName>
#include <QContactSyncTarget>
#include <QContactTimestamp>
#ifdef USING_QTPIM
#include <QContactExtendedDetail>
#include <QContactFamily>
#include <QContactGeoLocation>
#include <QContactVersion>
#endif

#include <QSqlError>

#include <QtDebug>

#ifdef USING_QTPIM
using namespace Conversion;
#endif

static const char *findConstituentsForAggregate =
        "\n SELECT contactId FROM Contacts WHERE contactId IN ("
        "\n SELECT secondId FROM Relationships WHERE firstId = :aggregateId AND type = 'Aggregates')";

static const char *findLocalForAggregate =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'local' AND contactId IN ("
        "\n SELECT secondId FROM Relationships WHERE firstId = :aggregateId AND type = 'Aggregates')";

static const char *findAggregateForContact =
        "\n SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates' AND secondId = :localId";

static const char *findMaximumContactId =
        "\n SELECT max(contactId) FROM Contacts";

static const char *findMatchForContact =
        "\n SELECT Matches.contactId, sum(Matches.score) AS total FROM ("
        "\n   SELECT contactId, 20 as score FROM Contacts"
        "\n      WHERE lowerLastName != '' AND lowerLastName = :lastName"
        "\n      AND lowerFirstName != '' AND :firstName != '' AND (lowerFirstName = :firstName OR lowerFirstName LIKE :firstPartial)"
        "\n   UNION"
        "\n   SELECT contactId, 12 as score FROM Contacts"
        "\n      WHERE (lowerLastName = '' OR :lastName = '')"
        "\n      AND lowerFirstName != '' AND (lowerFirstName = :firstName OR lowerFirstName LIKE :firstPartial)"
        "\n   UNION"
        "\n   SELECT contactId, 12 as score FROM Contacts"
        "\n      WHERE lowerLastName != '' AND lowerLastName = :lastName"
        "\n      AND (lowerFirstName = '' OR :firstName = '')"
        "\n   UNION"
        "\n   SELECT contactId, 3 as score FROM EmailAddresses WHERE lowerEmailAddress IN ( :email )"
        "\n   UNION"
        "\n   SELECT contactId, 3 as score FROM PhoneNumbers WHERE normalizedNumber IN ( :number )"
        "\n   UNION"
        "\n   SELECT contactId, 3 as score FROM OnlineAccounts WHERE lowerAccountUri IN ( :uri )"
        "\n   UNION"
        "\n   SELECT contactId, 1 as score FROM Nicknames WHERE lowerNickname != '' AND lowerNickname = :nickname"
        "\n ) AS Matches"
        "\n JOIN Contacts ON Contacts.contactId = Matches.contactId"
        "\n WHERE Contacts.syncTarget = 'aggregate'"
        "\n AND Matches.contactId <= :maxAggregateId"
        "\n AND Matches.contactId NOT IN ("
        "\n   SELECT DISTINCT secondId FROM Relationships WHERE firstId = :id AND type = 'IsNot'"
        "\n   UNION"
        "\n   SELECT DISTINCT firstId FROM Relationships WHERE secondId = :id AND type = 'IsNot'"
        "\n   UNION"
        "\n   SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates' AND secondId IN ("
        "\n     SELECT contactId FROM Contacts WHERE syncTarget = :syncTarget"
        "\n   )"
        "\n )"
        "\n GROUP BY Matches.contactId"
        "\n ORDER BY total DESC"
        "\n LIMIT 1";

static const char *selectAggregateContactIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'aggregate' AND contactId = :possibleAggregateId";

static const char *childlessAggregateIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'aggregate' AND contactId NOT IN ("
        "\n SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates')";

static const char *orphanContactIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget != 'aggregate' AND contactId NOT IN ("
        "\n SELECT DISTINCT secondId FROM Relationships WHERE type = 'Aggregates')";

static const char *checkContactExists =
        "\n SELECT COUNT(contactId), syncTarget FROM Contacts WHERE contactId = :contactId;";

static const char *existingContactIds =
        "\n SELECT DISTINCT contactId FROM Contacts;";

static const char *selfContactId =
        "\n SELECT DISTINCT contactId FROM Identities WHERE identity = :identity;";

static const char *insertContact =
        "\n INSERT INTO Contacts ("
        "\n  displayLabel,"
        "\n  firstName,"
        "\n  lowerFirstName,"
        "\n  lastName,"
        "\n  lowerLastName,"
        "\n  middleName,"
        "\n  prefix,"
        "\n  suffix,"
        "\n  customLabel,"
        "\n  syncTarget,"
        "\n  created,"
        "\n  modified,"
        "\n  gender,"
        "\n  isFavorite,"
        "\n  hasPhoneNumber,"
        "\n  hasEmailAddress,"
        "\n  hasOnlineAccount,"
        "\n  isOnline)"
        "\n VALUES ("
        "\n  :displayLabel,"
        "\n  :firstName,"
        "\n  :lowerFirstName,"
        "\n  :lastName,"
        "\n  :lowerLastName,"
        "\n  :middleName,"
        "\n  :prefix,"
        "\n  :suffix,"
        "\n  :customLabel,"
        "\n  :syncTarget,"
        "\n  :created,"
        "\n  :modified,"
        "\n  :gender,"
        "\n  :isFavorite,"
        "\n  :hasPhoneNumber,"
        "\n  :hasEmailAccount,"
        "\n  :hasOnlineAccount,"
        "\n  :isOnline);";

static const char *updateContact =
        "\n UPDATE Contacts SET"
        "\n  displayLabel = :displayLabel,"
        "\n  firstName = :firstName,"
        "\n  lowerFirstName = :lowerFirstName,"
        "\n  lastName = :lastName,"
        "\n  lowerLastName = :lowerLastName,"
        "\n  middleName = :middleName,"
        "\n  prefix = :prefix,"
        "\n  suffix = :suffix,"
        "\n  customLabel = :customLabel,"
        "\n  syncTarget = :syncTarget,"
        "\n  created = :created,"
        "\n  modified = :modified,"
        "\n  gender = :gender,"
        "\n  isFavorite = :isFavorite,"
        "\n  hasPhoneNumber = CASE WHEN :valueKnown = 1 THEN :value ELSE hasPhoneNumber END, "
        "\n  hasEmailAddress = CASE WHEN :valueKnown = 1 THEN :value ELSE hasEmailAddress END, "
        "\n  hasOnlineAccount = CASE WHEN :valueKnown = 1 THEN :value ELSE hasOnlineAccount END, "
        "\n  isOnline = CASE WHEN :valueKnown = 1 THEN :value ELSE isOnline END "
        "\n WHERE contactId = :contactId;";

static const char *removeContact =
        "\n DELETE FROM Contacts WHERE contactId = :contactId;";

static const char *existingRelationships =
        "\n SELECT firstId, secondId, type FROM Relationships;";

static const char *insertRelationship =
        "\n INSERT INTO Relationships ("
        "\n  firstId,"
        "\n  secondId,"
        "\n  type)"
        "\n VALUES ("
        "\n  :firstId,"
        "\n  :secondId,"
        "\n  :type);";

static const char *removeRelationship =
        "\n DELETE FROM Relationships"
        "\n WHERE firstId = :firstId AND secondId = :secondId AND type = :type;";

static const char *insertAddress =
        "\n INSERT INTO Addresses ("
        "\n  contactId,"
        "\n  street,"
        "\n  postOfficeBox,"
        "\n  region,"
        "\n  locality,"
        "\n  postCode,"
        "\n  country)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :street,"
        "\n  :postOfficeBox,"
        "\n  :region,"
        "\n  :locality,"
        "\n  :postCode,"
        "\n  :country)";

static const char *insertAnniversary =
        "\n INSERT INTO Anniversaries ("
        "\n  contactId,"
        "\n  originalDateTime,"
        "\n  calendarId,"
        "\n  subType)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :originalDateTime,"
        "\n  :calendarId,"
        "\n  :subType)";

static const char *insertAvatar =
        "\n INSERT INTO Avatars ("
        "\n  contactId,"
        "\n  imageUrl,"
        "\n  videoUrl,"
        "\n  avatarMetadata)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :imageUrl,"
        "\n  :videoUrl,"
        "\n  :avatarMetadata)";

static const char *insertBirthday =
        "\n INSERT INTO Birthdays ("
        "\n  contactId,"
        "\n  birthday,"
        "\n  calendarId)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :birthday,"
        "\n  :calendarId)";

static const char *insertEmailAddress =
        "\n INSERT INTO EmailAddresses ("
        "\n  contactId,"
        "\n  emailAddress,"
        "\n  lowerEmailAddress)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :emailAddress,"
        "\n  :lowerEmailAddress)";

static const char *insertGlobalPresence =
        "\n INSERT INTO GlobalPresences ("
        "\n  contactId,"
        "\n  presenceState,"
        "\n  timestamp,"
        "\n  nickname,"
        "\n  customMessage)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :presenceState,"
        "\n  :timestamp,"
        "\n  :nickname,"
        "\n  :customMessage)";

static const char *insertGuid =
        "\n INSERT INTO Guids ("
        "\n  contactId,"
        "\n  guid)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :guid)";

static const char *insertHobby =
        "\n INSERT INTO Hobbies ("
        "\n  contactId,"
        "\n  hobby)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :hobby)";

static const char *insertNickname =
        "\n INSERT INTO Nicknames ("
        "\n  contactId,"
        "\n  nickname,"
        "\n  lowerNickname)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :nickname,"
        "\n  :lowerNickname)";

static const char *insertNote =
        "\n INSERT INTO Notes ("
        "\n  contactId,"
        "\n  note)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :note)";

static const char *insertOnlineAccount =
        "\n INSERT INTO OnlineAccounts ("
        "\n  contactId,"
        "\n  accountUri,"
        "\n  lowerAccountUri,"
        "\n  protocol,"
        "\n  serviceProvider,"
        "\n  capabilities,"
        "\n  subTypes,"
        "\n  accountPath,"
        "\n  accountIconPath,"
        "\n  enabled)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :accountUri,"
        "\n  :lowerAccountUri,"
        "\n  :protocol,"
        "\n  :serviceProvider,"
        "\n  :capabilities,"
        "\n  :subTypes,"
        "\n  :accountPath,"
        "\n  :accountIconPath,"
        "\n  :enabled)";

static const char *insertOrganization =
        "\n INSERT INTO Organizations ("
        "\n  contactId,"
        "\n  name,"
        "\n  role,"
        "\n  title,"
        "\n  location,"
        "\n  department,"
        "\n  logoUrl)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :name,"
        "\n  :role,"
        "\n  :title,"
        "\n  :location,"
        "\n  :department,"
        "\n  :logoUrl)";

static const char *insertPhoneNumber =
        "\n INSERT INTO PhoneNumbers ("
        "\n  contactId,"
        "\n  phoneNumber,"
        "\n  subTypes,"
        "\n  normalizedNumber)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :phoneNumber,"
        "\n  :subTypes,"
        "\n  :normalizedNumber)";

static const char *insertPresence =
        "\n INSERT INTO Presences ("
        "\n  contactId,"
        "\n  presenceState,"
        "\n  timestamp,"
        "\n  nickname,"
        "\n  customMessage)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :presenceState,"
        "\n  :timestamp,"
        "\n  :nickname,"
        "\n  :customMessage)";

static const char *insertRingtone =
        "\n INSERT INTO Ringtones ("
        "\n  contactId,"
        "\n  audioRingtone,"
        "\n  videoRingtone)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :audioRingtone,"
        "\n  :videoRingtone)";

static const char *insertTag =
        "\n INSERT INTO Tags ("
        "\n  contactId,"
        "\n  tag)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :tag)";

static const char *insertUrl =
        "\n INSERT INTO Urls ("
        "\n  contactId,"
        "\n  url,"
        "\n  subTypes)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :url,"
        "\n  :subTypes)";

static const char *insertOriginMetadata =
        "\n INSERT INTO TpMetadata ("
        "\n  contactId,"
        "\n  telepathyId,"
        "\n  accountId,"
        "\n  accountEnabled)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :telepathyId,"
        "\n  :accountId,"
        "\n  :accountEnabled)";

static const char *insertDetail =
        "\n INSERT INTO Details ("
        "\n  contactId,"
        "\n  detailId,"
        "\n  detail,"
        "\n  detailUri,"
        "\n  linkedDetailUris,"
        "\n  contexts,"
        "\n  accessConstraints)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :detailId,"
        "\n  :detail,"
        "\n  :detailUri,"
        "\n  :linkedDetailUris,"
        "\n  :contexts,"
        "\n  :accessConstraints);";

static const char *insertIdentity =
        "\n INSERT OR REPLACE INTO Identities ("
        "\n  identity,"
        "\n  contactId)"
        "\n VALUES ("
        "\n  :identity,"
        "\n  :contactId);";


static QSqlQuery prepare(const char *statement, const QSqlDatabase &database)
{
    return ContactsDatabase::prepare(statement, database);
}

// Adapted from the inter-process mutex in QMF
// The first user creates the semaphore that all subsequent instances
// attach to.  We rely on undo semantics to release locked semaphores
// on process failure.
class ProcessMutex
{
    Semaphore m_semaphore;

public:
    ProcessMutex(const QString &path)
        : m_semaphore(path.toLatin1(), 1)
    {
    }

    bool lock()
    {
        return m_semaphore.decrement();
    }

    bool unlock()
    {
        return m_semaphore.increment();
    }

    bool isLocked() const
    {
        return (m_semaphore.value() == 0);
    }
};


ContactWriter::ContactWriter(const ContactsEngine &engine, const QSqlDatabase &database, ContactReader *reader)
    : m_engine(engine)
    , m_database(database)
    , m_databaseMutex(new ProcessMutex(database.databaseName()))
    , m_findConstituentsForAggregate(prepare(findConstituentsForAggregate, database))
    , m_findLocalForAggregate(prepare(findLocalForAggregate, database))
    , m_findAggregateForContact(prepare(findAggregateForContact, database))
    , m_findMaximumContactId(prepare(findMaximumContactId, database))
    , m_findMatchForContact(prepare(findMatchForContact, database))
    , m_selectAggregateContactIds(prepare(selectAggregateContactIds, database))
    , m_childlessAggregateIds(prepare(childlessAggregateIds, database))
    , m_orphanContactIds(prepare(orphanContactIds, database))
    , m_checkContactExists(prepare(checkContactExists, database))
    , m_existingContactIds(prepare(existingContactIds, database))
    , m_selfContactId(prepare(selfContactId, database))
    , m_insertContact(prepare(insertContact, database))
    , m_updateContact(prepare(updateContact, database))
    , m_removeContact(prepare(removeContact, database))
    , m_existingRelationships(prepare(existingRelationships, database))
    , m_insertRelationship(prepare(insertRelationship, database))
    , m_removeRelationship(prepare(removeRelationship, database))
    , m_insertAddress(prepare(insertAddress, database))
    , m_insertAnniversary(prepare(insertAnniversary, database))
    , m_insertAvatar(prepare(insertAvatar, database))
    , m_insertBirthday(prepare(insertBirthday, database))
    , m_insertEmailAddress(prepare(insertEmailAddress, database))
    , m_insertGlobalPresence(prepare(insertGlobalPresence, database))
    , m_insertGuid(prepare(insertGuid, database))
    , m_insertHobby(prepare(insertHobby, database))
    , m_insertNickname(prepare(insertNickname, database))
    , m_insertNote(prepare(insertNote, database))
    , m_insertOnlineAccount(prepare(insertOnlineAccount, database))
    , m_insertOrganization(prepare(insertOrganization, database))
    , m_insertPhoneNumber(prepare(insertPhoneNumber, database))
    , m_insertPresence(prepare(insertPresence, database))
    , m_insertRingtone(prepare(insertRingtone, database))
    , m_insertTag(prepare(insertTag, database))
    , m_insertUrl(prepare(insertUrl, database))
    , m_insertOriginMetadata(prepare(insertOriginMetadata, database))
    , m_insertDetail(prepare(insertDetail, database))
    , m_insertIdentity(prepare(insertIdentity, database))
    , m_removeAddress(prepare("DELETE FROM Addresses WHERE contactId = :contactId;", database))
    , m_removeAnniversary(prepare("DELETE FROM Anniversaries WHERE contactId = :contactId;", database))
    , m_removeAvatar(prepare("DELETE FROM Avatars WHERE contactId = :contactId;", database))
    , m_removeBirthday(prepare("DELETE FROM Birthdays WHERE contactId = :contactId;", database))
    , m_removeEmailAddress(prepare("DELETE FROM EmailAddresses WHERE contactId = :contactId;", database))
    , m_removeGlobalPresence(prepare("DELETE FROM GlobalPresences WHERE contactId = :contactId;", database))
    , m_removeGuid(prepare("DELETE FROM Guids WHERE contactId = :contactId;", database))
    , m_removeHobby(prepare("DELETE FROM Hobbies WHERE contactId = :contactId;", database))
    , m_removeNickname(prepare("DELETE FROM Nicknames WHERE contactId = :contactId;", database))
    , m_removeNote(prepare("DELETE FROM Notes WHERE contactId = :contactId;", database))
    , m_removeOnlineAccount(prepare("DELETE FROM OnlineAccounts WHERE contactId = :contactId;", database))
    , m_removeOrganization(prepare("DELETE FROM Organizations WHERE contactId = :contactId;", database))
    , m_removePhoneNumber(prepare("DELETE FROM PhoneNumbers WHERE contactId = :contactId;", database))
    , m_removePresence(prepare("DELETE FROM Presences WHERE contactId = :contactId;", database))
    , m_removeRingtone(prepare("DELETE FROM Ringtones WHERE contactId = :contactId;", database))
    , m_removeTag(prepare("DELETE FROM Tags WHERE contactId = :contactId;", database))
    , m_removeUrl(prepare("DELETE FROM Urls WHERE contactId = :contactId;", database))
    , m_removeOriginMetadata(prepare("DELETE FROM TpMetadata WHERE contactId = :contactId;", database))
    , m_removeDetail(prepare("DELETE FROM Details WHERE contactId = :contactId AND detail = :detail;", database))
    , m_removeIdentity(prepare("DELETE FROM Identities WHERE identity = :identity;", database))
    , m_reader(reader)
{
}

ContactWriter::~ContactWriter()
{
}

bool ContactWriter::beginTransaction()
{
    // We use a cross-process mutex to ensure only one process can
    // write to the DB at once.  Without locking, sqlite will back off
    // on write contention, and the backed-off process may never get access
    // if other processes are performing regular writes.
    if (m_databaseMutex->lock()) {
        if (m_database.transaction())
            return true;

        m_databaseMutex->unlock();
    }

    return false;
}

bool ContactWriter::commitTransaction()
{
    if (!m_database.commit()) {
        qWarning() << "Commit error:" << m_database.lastError();
        rollbackTransaction();
        return false;
    }

    if (m_databaseMutex->isLocked()) {
        m_databaseMutex->unlock();
    } else {
        qWarning() << "Lock error: no lock held on commit";
    }

    if (!m_addedIds.isEmpty()) {
        ContactNotifier::contactsAdded(m_addedIds.toList());
        m_addedIds.clear();
    }
    if (!m_changedIds.isEmpty()) {
        ContactNotifier::contactsChanged(m_changedIds.toList());
        m_changedIds.clear();
    }
    if (!m_removedIds.isEmpty()) {
        ContactNotifier::contactsRemoved(m_removedIds.toList());
        m_removedIds.clear();
    }
    return true;
}

void ContactWriter::rollbackTransaction()
{
    m_database.rollback();
    if (m_databaseMutex->isLocked()) {
        m_databaseMutex->unlock();
    } else {
        qWarning() << "Lock error: no lock held on rollback";
    }

    m_removedIds.clear();
    m_changedIds.clear();
    m_addedIds.clear();
}

QContactManager::Error ContactWriter::setIdentity(
        ContactsDatabase::Identity identity, QContactIdType contactId)
{
    QSqlQuery *query = 0;

    quint32 dbId = ContactId::databaseId(contactId);
    if (dbId != 0) {
        m_insertIdentity.bindValue(0, identity);
        m_insertIdentity.bindValue(1, dbId);
        query = &m_insertIdentity;
    } else {
        m_removeIdentity.bindValue(0, identity);
        query = &m_removeIdentity;
    }

    if (query->exec()) {
        // Notify..
        query->finish();
        return QContactManager::NoError;
    } else {
        return QContactManager::UnspecifiedError;
    }
}

// This function is currently unused - but the way we currently build up the
// relationships query is hideously inefficient, so in the future we should
// rewrite this bindRelationships function and use execBatch().
/*
static QContactManager::Error bindRelationships(
        QSqlQuery *query,
        const QList<QContactRelationship> &relationships,
        QMap<int, QContactManager::Error> *errorMap,
        QSet<QContactLocalId> *contactIds,
        QMultiMap<QContactLocalId, QPair<QString, QContactLocalId> > *bucketedRelationships,
        int *removedDuplicatesCount)
{
    QVariantList firstIds;
    QVariantList secondIds;
    QVariantList types;
    *removedDuplicatesCount = 0;

    for (int i = 0; i < relationships.count(); ++i) {
        const QContactRelationship &relationship = relationships.at(i);
        const QContactLocalId firstId = relationship.first().localId();
        const QContactLocalId secondId = relationship.second().localId();
        const QString &type = relationship.relationshipType();

        if (firstId == 0 || secondId == 0) {
            if (errorMap)
                errorMap->insert(i, QContactManager::UnspecifiedError);
        } else if (type.isEmpty()) {
            if (errorMap)
                errorMap->insert(i, QContactManager::UnspecifiedError);
        } else {
            if (bucketedRelationships->find(firstId, QPair<QString, QContactLocalId>(type, secondId)) != bucketedRelationships->end()) {
                // this relationship is already represented in our database.
                // according to the semantics defined in tst_qcontactmanager,
                // we allow saving duplicates by "overwriting" (with identical values)
                // which means that we simply "drop" this one from the list
                // of relationships to add to the database.
                *removedDuplicatesCount += 1;
            } else {
                // this relationships has not yet been represented in our database.
                firstIds.append(firstId - 1);
                secondIds.append(secondId - 1);
                types.append(type);

                contactIds->insert(firstId);
                contactIds->insert(secondId);

                bucketedRelationships->insert(firstId, QPair<QString, QContactLocalId>(type, secondId));
            }
        }
    }

    if (firstIds.isEmpty() && *removedDuplicatesCount == 0) {
        // if we "successfully overwrote" some duplicates, it's not an error.
        return QContactManager::UnspecifiedError;
    }

    if (firstIds.size() == 1) {
        query->bindValue(0, firstIds.at(0).toUInt());
        query->bindValue(1, secondIds.at(0).toUInt());
        query->bindValue(2, types.at(0).toString());
    } else if (firstIds.size() > 1) {
        query->bindValue(0, firstIds);
        query->bindValue(1, secondIds);
        query->bindValue(2, types);
    }

    return QContactManager::NoError;
}
*/

QContactManager::Error ContactWriter::save(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    if (relationships.isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !beginTransaction()) {
        qWarning() << "Unable to begin database transaction while saving relationships";
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = saveRelationships(relationships, errorMap);
    if (error != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback if we created a transaction.
            rollbackTransaction();
            return error;
        }
    }

    if (!withinTransaction && !commitTransaction()) {
        qWarning() << "Failed to commit database after relationship save";
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

template<typename T>
QString relationshipString(T type)
{
#ifdef USING_QTPIM
    return type();
#else
    return type;
#endif
}

QContactManager::Error ContactWriter::saveRelationships(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap)
{
#ifdef USING_QTPIM
    static const QString uri(QString::fromLatin1("qtcontacts:org.nemomobile.contacts.sqlite:"));
#else
    static const QString uri(QString::fromLatin1("org.nemomobile.contacts.sqlite"));
#endif

    // in order to perform duplicate detection we build up the following datastructure.
    QMultiMap<quint32, QPair<QString, quint32> > bucketedRelationships; // first id to <type, second id>.
    {
        if (!m_existingRelationships.exec()) {
            qWarning() << "Failed to fetch existing relationships for duplicate detection during insert:";
            qWarning() << m_existingRelationships.lastError();
            return QContactManager::UnspecifiedError;
        }

        while (m_existingRelationships.next()) {
            quint32 fid = m_existingRelationships.value(0).toUInt();
            quint32 sid = m_existingRelationships.value(1).toUInt();
            QString rt = m_existingRelationships.value(2).toString();
            bucketedRelationships.insert(fid, qMakePair(rt, sid));
        }

        m_existingRelationships.finish();
    }

    // in order to perform validity detection we build up the following set.
    // XXX TODO: use foreign key constraint or similar in Relationships table?
    QSet<quint32> validContactIds;
    {
        if (!m_existingContactIds.exec()) {
            qWarning() << "Failed to fetch existing contacts for validity detection during insert:";
            qWarning() << m_existingContactIds.lastError();
            return QContactManager::UnspecifiedError;
        }

        while (m_existingContactIds.next()) {
            validContactIds.insert(m_existingContactIds.value(0).toUInt());
        }

        m_existingContactIds.finish();
    }

    QList<quint32> firstIdsToBind;
    QList<quint32> secondIdsToBind;
    QList<QString> typesToBind;

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    QSet<quint32> aggregatesAffected;
#endif

    QSqlQuery multiInsertQuery(m_database);
    QString queryString = QLatin1String("INSERT INTO Relationships");
    int realInsertions = 0;
    int invalidInsertions = 0;
    for (int i = 0; i < relationships.size(); ++i) {
        const QContactRelationship &relationship = relationships.at(i);

#ifdef USING_QTPIM
        QContactId first(relationship.first().id());
        QContactId second(relationship.second().id());
#else
        QContactId first(relationship.first());
        QContactId second(relationship.second());
#endif

        const quint32 firstId = ContactId::databaseId(first);
        const quint32 secondId = ContactId::databaseId(second);
        const QString &type = relationship.relationshipType();

        if ((firstId == secondId)
                || (!first.managerUri().isEmpty() &&
#ifdef USING_QTPIM
                    !first.managerUri().startsWith(uri)
#else
                    first.managerUri() != uri
#endif
                   )
                || (!second.managerUri().isEmpty() &&
#ifdef USING_QTPIM
                    !second.managerUri().startsWith(uri)
#else
                    second.managerUri() != uri
#endif
                   )
                || (!validContactIds.contains(firstId) || !validContactIds.contains(secondId))) {
            // invalid contact specified in relationship, don't insert.
            invalidInsertions += 1;
            if (errorMap)
                errorMap->insert(i, QContactManager::InvalidRelationshipError);
            continue;
        }

        if (bucketedRelationships.find(firstId, qMakePair(type, secondId)) != bucketedRelationships.end()) {
            // duplicate, don't insert.
            continue;
        } else {
            if (realInsertions == 0) {
                queryString += QString(QLatin1String("\n SELECT :firstId%1 as firstId, :secondId%1 as secondId, :type%1 as type"))
                                      .arg(QString::number(realInsertions));
            } else {
                queryString += QString(QLatin1String("\n UNION SELECT :firstId%1, :secondId%1, :type%1"))
                                      .arg(QString::number(realInsertions));
            }
            firstIdsToBind.append(firstId);
            secondIdsToBind.append(secondId);
            typesToBind.append(type);
            bucketedRelationships.insert(firstId, qMakePair(type, secondId));
            realInsertions += 1;

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
            if (type == relationshipString(QContactRelationship::Aggregates)) {
                // This aggregate needs to be regenerated
                aggregatesAffected.insert(firstId);
            }
#endif
        }
    }

    if (realInsertions > 0 && !multiInsertQuery.prepare(queryString)) {
        qWarning() << "Failed to prepare multiple insert relationships query";
        qWarning() << multiInsertQuery.lastError();
        qWarning() << "Query:\n" << queryString;
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < realInsertions; ++i) {
        multiInsertQuery.bindValue(QString(QLatin1String(":firstId%1")).arg(QString::number(i)), firstIdsToBind.at(i));
        multiInsertQuery.bindValue(QString(QLatin1String(":secondId%1")).arg(QString::number(i)), secondIdsToBind.at(i));
        multiInsertQuery.bindValue(QString(QLatin1String(":type%1")).arg(QString::number(i)), typesToBind.at(i));
    }

    if (realInsertions > 0 && !multiInsertQuery.exec()) {
        qWarning() << "Failed to insert relationships";
        qWarning() << multiInsertQuery.lastError();
        qWarning() << "Query:\n" << queryString;
        return QContactManager::UnspecifiedError;
    }

    if (invalidInsertions > 0) {
        return QContactManager::InvalidRelationshipError;
    }

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (!aggregatesAffected.isEmpty()) {
        regenerateAggregates(aggregatesAffected.toList(), DetailList(), true);
    }
#endif

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    if (relationships.isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !beginTransaction()) {
        qWarning() << "Unable to begin database transaction while removing relationships";
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = removeRelationships(relationships, errorMap);
    if (error != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback if we created a transaction.
            rollbackTransaction();
            return error;
        }
    }

    if (!withinTransaction && !commitTransaction()) {
        qWarning() << "Failed to commit database after relationship removal";
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::removeRelationships(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap)
{
    // in order to perform existence detection we build up the following datastructure.
    QMultiMap<quint32, QPair<QString, quint32> > bucketedRelationships; // first id to <type, second id>.
    {
        if (!m_existingRelationships.exec()) {
            qWarning() << "Failed to fetch existing relationships for duplicate detection during insert:";
            qWarning() << m_existingRelationships.lastError();
            return QContactManager::UnspecifiedError;
        }

        while (m_existingRelationships.next()) {
            quint32 fid = m_existingRelationships.value(0).toUInt();
            quint32 sid = m_existingRelationships.value(1).toUInt();
            QString rt = m_existingRelationships.value(2).toString();
            bucketedRelationships.insert(fid, qMakePair(rt, sid));
        }

        m_existingRelationships.finish();
    }

    QContactManager::Error worstError = QContactManager::NoError;
    QSet<QContactRelationship> alreadyRemoved;
#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    QSet<quint32> aggregatesAffected;
#endif
    bool removeInvalid = false;
    for (int i = 0; i < relationships.size(); ++i) {
        QContactRelationship curr = relationships.at(i);
        if (alreadyRemoved.contains(curr)) {
            continue;
        }

        quint32 currFirst = ContactId::databaseId(curr.first());
        quint32 currSecond = ContactId::databaseId(curr.second());

        if (bucketedRelationships.find(currFirst, qMakePair(curr.relationshipType(), currSecond)) == bucketedRelationships.end()) {
            removeInvalid = true;
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            continue;
        }

        QSqlQuery removeRelationship(m_database);
        if (!removeRelationship.prepare("DELETE FROM Relationships WHERE firstId = :firstId AND secondId = :secondId AND type = :type;")) {
            qWarning() << "Failed to prepare remove relationship";
            qWarning() << removeRelationship.lastError();
            worstError = QContactManager::UnspecifiedError;
            if (errorMap)
                errorMap->insert(i, worstError);
            continue;
        }

        QString type(curr.relationshipType());

        removeRelationship.bindValue(":firstId", currFirst);
        removeRelationship.bindValue(":secondId", currSecond);
        removeRelationship.bindValue(":type", type);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
        if (type == relationshipString(QContactRelationship::Aggregates)) {
            // This aggregate needs to be regenerated
            aggregatesAffected.insert(currFirst);
        }
#endif

        if (!removeRelationship.exec()) {
            qWarning() << "Failed to remove relationship";
            qWarning() << removeRelationship.lastError();
            worstError = QContactManager::UnspecifiedError;
            if (errorMap)
                errorMap->insert(i, worstError);
            continue;
        }

        alreadyRemoved.insert(curr);
    }

    if (removeInvalid) {
        return QContactManager::DoesNotExistError;
    }

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    // remove any aggregates that no longer aggregate any contacts.
    QList<QContactIdType> removedIds;
    QContactManager::Error removeError = removeChildlessAggregates(&removedIds);
    if (removeError != QContactManager::NoError)
        return removeError;

    foreach (const QContactIdType &id, removedIds) {
        m_removedIds.insert(id);
        aggregatesAffected.remove(ContactId::databaseId(id));
    }

    if (!aggregatesAffected.isEmpty()) {
        regenerateAggregates(aggregatesAffected.toList(), DetailList(), true);
    }

    // Some contacts may need to have new aggregates created
    QContactManager::Error aggregateError = aggregateOrphanedContacts(true);
    if (aggregateError != QContactManager::NoError)
        return aggregateError;
#endif

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(const QList<QContactIdType> &contactIds, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    if (contactIds.isEmpty())
        return QContactManager::NoError;

    // grab the self-contact id so we can avoid removing it.
    quint32 selfContactId = 0;
    m_selfContactId.bindValue(":identity", ContactsDatabase::SelfContactId);
    if (!m_selfContactId.exec()) {
        qWarning() << "Failed to fetch self contact id during remove";
        qWarning() << m_selfContactId.lastError();
        return QContactManager::UnspecifiedError;
    }
    if (m_selfContactId.next()) {
        selfContactId = m_selfContactId.value(0).toUInt();
    }
    m_selfContactId.finish();

    // grab the existing contact ids so that we can perform removal detection
    // XXX TODO: for perf, remove this check.  Less conformant, but client
    // shouldn't care (ie, not exists == has been removed).
    QSet<quint32> existingContactIds;
    if (!m_existingContactIds.exec()) {
        qWarning() << "Failed to fetch existing contact ids during remove";
        qWarning() << m_existingContactIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_existingContactIds.next()) {
        existingContactIds.insert(m_existingContactIds.value(0).toUInt());
    }
    m_existingContactIds.finish();

    // determine which contacts we actually need to remove
    QContactManager::Error error = QContactManager::NoError;
    QList<QContactIdType> realRemoveIds;
    QVariantList boundRealRemoveIds;
    for (int i = 0; i < contactIds.size(); ++i) {
        QContactIdType currId = contactIds.at(i);
        quint32 dbId = ContactId::databaseId(currId);
        if (selfContactId > 0 && dbId == selfContactId) {
            if (errorMap)
                errorMap->insert(i, QContactManager::BadArgumentError);
            error = QContactManager::BadArgumentError;
        } else if (existingContactIds.contains(dbId)) {
            realRemoveIds.append(currId);
            boundRealRemoveIds.append(dbId);
        } else {
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            error = QContactManager::DoesNotExistError;
        }
    }

#ifndef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    // If we don't perform aggregation, we simply need to remove every
    // (valid, non-self) contact specified in the list.
    if (realRemoveIds.size() > 0) {
        if (!withinTransaction && !beginTransaction()) {
            // if we are not already within a transaction, create a transaction.
            qWarning() << "Unable to begin database transaction while removing contacts";
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.bindValue(QLatin1String(":contactId"), boundRealRemoveIds);
        if (!m_removeContact.execBatch()) {
            qWarning() << "Failed to remove contacts";
            qWarning() << m_removeContact.lastError();
            if (!withinTransaction) {
                // only rollback if we created a transaction.
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.finish();
        foreach (const QContactIdType &rrid, realRemoveIds) {
            m_removedIds.insert(rrid);
        }
        if (!withinTransaction && !commitTransaction()) {
            // only commit if we created a transaction.
            qWarning() << "Failed to commit removal";
            return QContactManager::UnspecifiedError;
        }
    }
    return error;
#else
    // grab the ids of aggregate contacts which aggregate any of the contacts
    // which we're about to remove.  We will regenerate them after successful
    // remove.  Also grab the ids of aggregates which are being removed, so
    // we can remove all related contacts.
    QList<quint32> aggregatesOfRemoved;
    QList<quint32> aggregatesToRemove;
    m_selectAggregateContactIds.bindValue(":possibleAggregateId", boundRealRemoveIds);
    if (!m_selectAggregateContactIds.execBatch()) {
        qWarning() << "Failed to select aggregate contact ids during remove";
        qWarning() << m_selectAggregateContactIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_selectAggregateContactIds.next()) {
        aggregatesToRemove.append(m_selectAggregateContactIds.value(0).toUInt());
    }
    m_selectAggregateContactIds.finish();

    m_findAggregateForContact.bindValue(":localId", boundRealRemoveIds);
    if (!m_findAggregateForContact.execBatch()) {
        qWarning() << "Failed to fetch aggregator contact ids during remove";
        qWarning() << m_findAggregateForContact.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_findAggregateForContact.next()) {
        aggregatesOfRemoved.append(m_findAggregateForContact.value(0).toUInt());
    }
    m_findAggregateForContact.finish();

    QVariantList boundNonAggregatesToRemove;
    QVariantList boundAggregatesToRemove;
    foreach (const QContactIdType &rrid, realRemoveIds) {
        quint32 dbId = ContactId::databaseId(rrid);
        if (!aggregatesToRemove.contains(dbId)) {
            // this is a non-aggregate contact which we need to remove
            boundNonAggregatesToRemove.append(dbId);
        } else {
            // this is an aggregate contact which we need to remove
            boundAggregatesToRemove.append(dbId);
        }
    }

    if (!withinTransaction && !beginTransaction()) {
        // only create a transaction if we're not already within one
        qWarning() << "Unable to begin database transaction while removing contacts";
        return QContactManager::UnspecifiedError;
    }

    // remove the non-aggregate contacts
    if (boundNonAggregatesToRemove.size() > 0) {
        m_removeContact.bindValue(QLatin1String(":contactId"), boundNonAggregatesToRemove);
        if (!m_removeContact.execBatch()) {
            qWarning() << "Failed to removed non-aggregate contacts";
            qWarning() << m_removeContact.lastError();
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        }

        m_removeContact.finish();
    }

    // remove the aggregate contacts - and any contacts they aggregate
    if (boundAggregatesToRemove.size() > 0) {
        // first, get the list of contacts which are aggregated by the aggregate contacts we are going to remove
        m_findConstituentsForAggregate.bindValue(":aggregateId", boundAggregatesToRemove);
        if (!m_findConstituentsForAggregate.execBatch()) {
            qWarning() << "Failed to fetch contacts aggregated by removed aggregates";
            qWarning() << m_findConstituentsForAggregate.lastError();
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        }
        while (m_findConstituentsForAggregate.next()) {
            quint32 dbId = m_findConstituentsForAggregate.value(0).toUInt();
            boundAggregatesToRemove.append(dbId); // we just add it to the big list of bound "remove these"
            realRemoveIds.append(ContactId::apiId(dbId));
        }
        m_findConstituentsForAggregate.finish();

        // remove the aggregates + the aggregated
        m_removeContact.bindValue(QLatin1String(":contactId"), boundAggregatesToRemove);
        if (!m_removeContact.execBatch()) {
            qWarning() << "Failed to removed aggregate contacts (and the contacts they aggregate)";
            qWarning() << m_removeContact.lastError();
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.finish();
    }

    // removing aggregates if they no longer aggregate any contacts.
    QContactManager::Error removeError = removeChildlessAggregates(&realRemoveIds);
    if (removeError != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback the transaction if we created it
            rollbackTransaction();
        }
        return removeError;
    }

    foreach (const QContactIdType &id, realRemoveIds)
        m_removedIds.insert(id);

    // And notify of any removals.
    if (realRemoveIds.size() > 0) {
        // update our "regenerate list" by purging removed contacts
        foreach (const QContactIdType &removedId, realRemoveIds) {
            aggregatesOfRemoved.removeAll(ContactId::databaseId(removedId));
        }
    }

    // Now regenerate our remaining aggregates as required.
    if (aggregatesOfRemoved.size() > 0) {
        regenerateAggregates(aggregatesOfRemoved, DetailList(), true);
    }

    // Success!  If we created a transaction, commit.
    if (!withinTransaction && !commitTransaction()) {
        qWarning() << "Failed to commit database after removal";
        return QContactManager::UnspecifiedError;
    }

    return error;
#endif
}

template<typename T>
#ifdef USING_QTPIM
QContactDetail::DetailType detailType()
#else
const char *detailType()
#endif
{
#ifdef USING_QTPIM
    return T::Type;
#else
    return T::DefinitionName.latin1();
#endif
}

#ifdef USING_QTPIM
QContactDetail::DetailType detailType(const QContactDetail &detail)
#else
QString detailType(const QContactDetail &detail)
#endif
{
#ifdef USING_QTPIM
    return detail.type();
#else
    return detail.definitionName();
#endif
}

#ifdef USING_QTPIM
template<typename T>
void insert(QMap<QContactDetail::DetailType, const char *> &map, const char *name)
{
    map.insert(T::Type, name);
}

#define PREFIX_LENGTH 8
#define STRINGIZE(T) #T
#define INSERT(map,T) insert<T>(map, STRINGIZE(T) + PREFIX_LENGTH)

QMap<QContactDetail::DetailType, const char *> getDetailTypeNames()
{
    QMap<QContactDetail::DetailType, const char *> rv;

    INSERT(rv, QContactAddress);
    INSERT(rv, QContactAnniversary);
    INSERT(rv, QContactAvatar);
    INSERT(rv, QContactBirthday);
    INSERT(rv, QContactDisplayLabel);
    INSERT(rv, QContactEmailAddress);
    INSERT(rv, QContactExtendedDetail);
    INSERT(rv, QContactFamily);
    INSERT(rv, QContactFavorite);
    INSERT(rv, QContactGender);
    INSERT(rv, QContactGeoLocation);
    INSERT(rv, QContactGlobalPresence);
    INSERT(rv, QContactGuid);
    INSERT(rv, QContactHobby);
    INSERT(rv, QContactName);
    INSERT(rv, QContactNickname);
    INSERT(rv, QContactNote);
    INSERT(rv, QContactOnlineAccount);
    INSERT(rv, QContactOrganization);
    INSERT(rv, QContactPhoneNumber);
    INSERT(rv, QContactPresence);
    INSERT(rv, QContactRingtone);
    INSERT(rv, QContactSyncTarget);
    INSERT(rv, QContactTag);
    INSERT(rv, QContactTimestamp);
    INSERT(rv, QContactType);
    INSERT(rv, QContactUrl);
    INSERT(rv, QContactVersion);

    // Our extensions:
    INSERT(rv, QContactOriginMetadata);
    INSERT(rv, QContactStatusFlags);

    return rv;
}

#undef INSERT
#undef STRINGIZE
#undef PREFIX_LENGTH

const char *detailTypeName(QContactDetail::DetailType type)
{
    static const QMap<QContactDetail::DetailType, const char *> names(getDetailTypeNames());

    QMap<QContactDetail::DetailType, const char *>::const_iterator it = names.find(type);
    if (it != names.end()) {
        return *it;
    }
    return 0;
}

#else
const QString &detailTypeName(const QString &type)
{
    return type;
}
#endif

template<typename T>
const char *detailTypeName()
{
#ifdef USING_QTPIM
    return detailTypeName(T::Type);
#else
    return T::DefinitionName.latin1();
#endif
}

QString detailTypeName(const QContactDetail &detail)
{
#ifdef USING_QTPIM
    return QString::fromLatin1(detailTypeName(detail.type()));
#else
    return detail.definitionName();
#endif
}

template<typename T>
static bool detailListContains(const ContactWriter::DetailList &list)
{
    return list.contains(detailType<T>());
}

// Presence and GlobalPresence should be handled together
template<>
bool detailListContains<QContactGlobalPresence>(const ContactWriter::DetailList &list)
{
    return list.contains(detailType<QContactPresence>()) || list.contains(detailType<QContactGlobalPresence>());
}
template<>
bool detailListContains<QContactPresence>(const ContactWriter::DetailList &list)
{
    return list.contains(detailType<QContactPresence>()) || list.contains(detailType<QContactGlobalPresence>());
}

static bool detailListContains(const ContactWriter::DetailList &list, const QContactDetail &detail)
{
#ifdef USING_QTPIM
    return list.contains(detail.type());
#else
    return list.contains(detail.definitionName());
#endif
}

template <typename T> bool ContactWriter::removeCommonDetails(
            quint32 contactId, QContactManager::Error *error)
{
    m_removeDetail.bindValue(0, contactId);
    m_removeDetail.bindValue(1, detailTypeName<T>());

    if (!m_removeDetail.exec()) {
        qWarning() << "Failed to remove common detail for" << detailTypeName<T>();
        qWarning() << m_removeDetail.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    m_removeDetail.finish();
    return true;
}

template<typename T, typename F>
QVariant detailValue(const T &detail, F field)
{
#ifdef USING_QTPIM
    return detail.value(field);
#else
    return detail.variantValue(field);
#endif
}

#ifdef USING_QTPIM
typedef QMap<int, QVariant> DetailMap;
#else
typedef QVariantMap DetailMap;
#endif

DetailMap detailValues(const QContactDetail &detail)
{
#ifdef USING_QTPIM
    return detail.values();
#else
    return detail.variantValues();
#endif
}

static bool variantEqual(const QVariant &lhs, const QVariant &rhs)
{
#ifdef USING_QTPIM
    // Work around incorrect result from QVariant::operator== when variants contain QList<int>
    static const int QListIntType = QMetaType::type("QList<int>");

    const int lhsType = lhs.userType();
    if (lhsType != rhs.userType()) {
        return false;
    }

    if (lhsType == QListIntType) {
        return (lhs.value<QList<int> >() == rhs.value<QList<int> >());
    }
#endif
    return (lhs == rhs);
}

static bool detailValuesEqual(const QContactDetail &lhs, const QContactDetail &rhs)
{
    const DetailMap lhsValues(detailValues(lhs));
    const DetailMap rhsValues(detailValues(rhs));

    if (lhsValues.count() != rhsValues.count()) {
        return false;
    }

    DetailMap::const_iterator lit = lhsValues.constBegin(), lend = lhsValues.constEnd();
    DetailMap::const_iterator rit = rhsValues.constBegin();
    for ( ; lit != lend; ++lit, ++rit) {
        if (!variantEqual(*lit, *rit)) {
            return false;
        }
    }

    return true;
}

static bool detailValuesSuperset(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // True if all values in rhs are present in lhs
    const DetailMap lhsValues(detailValues(lhs));
    const DetailMap rhsValues(detailValues(rhs));

    if (lhsValues.count() < rhsValues.count()) {
        return false;
    }

    foreach (const DetailMap::key_type &key, rhsValues.keys()) {
        if (!variantEqual(lhsValues[key], rhsValues[key])) {
            return false;
        }
    }

    return true;
}

static bool detailsEquivalent(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // Same as operator== except ignores differences in accessConstraints values
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesEqual(lhs, rhs);
}

static bool detailsSuperset(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // True is lhs is a superset of rhs
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesSuperset(lhs, rhs);
}

QVariant detailLinkedUris(const QContactDetail &detail)
{
    static const QString separator = QString::fromLatin1(";");

    return QVariant(detail.linkedDetailUris().join(separator));
}

#ifdef USING_QTPIM
QMap<int, QString> contextTypes()
{
    QMap<int, QString> rv;

    rv.insert(QContactDetail::ContextHome, QString::fromLatin1("Home"));
    rv.insert(QContactDetail::ContextWork, QString::fromLatin1("Work"));
    rv.insert(QContactDetail::ContextOther, QString::fromLatin1("Other"));
    rv.insert(QContactDetail__ContextDefault, QString::fromLatin1("Default"));
    rv.insert(QContactDetail__ContextLarge, QString::fromLatin1("Large"));

    return rv;
}

QString contextString(int type)
{
    static const QMap<int, QString> types(contextTypes());

    QMap<int, QString>::const_iterator it = types.find(type);
    if (it != types.end()) {
        return *it;
    }
    return QString();
}
#endif

QVariant detailContexts(const QContactDetail &detail)
{
    static const QString separator = QString::fromLatin1(";");

    QStringList contexts;
#ifdef USING_QTPIM
    foreach (int context, detail.contexts()) {
        contexts.append(contextString(context));
    }
#else
    foreach (const QString &context, detail.contexts()) {
        contexts.append(context);
    }
#endif
    return QVariant(contexts.join(separator));
}

template <typename T> bool ContactWriter::writeCommonDetails(
            quint32 contactId, const QVariant &detailId, const T &detail, QContactManager::Error *error)
{
    const QVariant detailUri = detailValue(detail, QContactDetail::FieldDetailUri);
    const QVariant linkedDetailUris = detailLinkedUris(detail);
    const QVariant contexts = detailContexts(detail);
    int accessConstraints = static_cast<int>(detail.accessConstraints());

    if (detailUri.isValid() || linkedDetailUris.isValid() || contexts.isValid() || accessConstraints > 0) {
        m_insertDetail.bindValue(0, contactId);
        m_insertDetail.bindValue(1, detailId);
        m_insertDetail.bindValue(2, detailTypeName<T>());
        m_insertDetail.bindValue(3, detailUri);
        m_insertDetail.bindValue(4, linkedDetailUris);
        m_insertDetail.bindValue(5, contexts);
        m_insertDetail.bindValue(6, accessConstraints);

        if (!m_insertDetail.exec()) {
            qWarning() << "Failed to write common details for" << detailTypeName<T>();
            qWarning() << m_insertDetail.lastError();
            qWarning() << "detailUri:" << detailUri.value<QString>() << "linkedDetailUris:" << linkedDetailUris.value<QString>();
            *error = QContactManager::UnspecifiedError;
            return false;
        }

        m_insertDetail.finish();
    }
    return true;
}

template <typename T> bool ContactWriter::writeDetails(
        quint32 contactId,
        QContact *contact,
        QSqlQuery &removeQuery,
        const DetailList &definitionMask,
        QContactManager::Error *error)
{
    if (!definitionMask.isEmpty() && !detailListContains<T>(definitionMask))
        return true;

    if (!removeCommonDetails<T>(contactId, error))
        return false;

    removeQuery.bindValue(0, contactId);
    if (!removeQuery.exec()) {
        qWarning() << "Failed to remove existing details for" << detailTypeName<T>();
        qWarning() << removeQuery.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }
    removeQuery.finish();

    foreach (const T &detail, contact->details<T>()) {
        QSqlQuery &query = bindDetail(contactId, detail);
        if (!query.exec()) {
            qWarning() << "Failed to write details for" << detailTypeName<T>();
            qWarning() << query.lastError();
            *error = QContactManager::UnspecifiedError;
            return false;
        }

        QVariant detailId = query.lastInsertId();
        query.finish();

        if (!writeCommonDetails(contactId, detailId, detail, error)) {
            return false;
        }
    }
    return true;
}

static bool betterPresence(const QContactPresence &detail, const QContactPresence &best)
{
    if (best.isEmpty())
        return true;

    if (best.presenceState() == QContactPresence::PresenceUnknown) {
        return (detail.presenceState() != QContactPresence::PresenceUnknown);
    }

    return (detail.presenceState() < best.presenceState());
}

QContactManager::Error ContactWriter::save(
            QList<QContact> *contacts,
            const DetailList &definitionMask,
            QMap<int, bool> *aggregatesUpdated,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate)
{
    if (contacts->isEmpty())
        return QContactManager::NoError;

    // Check that all of the contacts have the same sync target.
    // Note that empty == "local" for all intents and purposes.
    if (!withinAggregateUpdate) {
        QString batchSyncTarget;
        for (int i = 0; i < contacts->count(); ++i) {
            // retrieve current contact's sync target
            QString currSyncTarget = (*contacts)[i].detail<QContactSyncTarget>().syncTarget();
            if (currSyncTarget.isEmpty()) {
                currSyncTarget = QLatin1String("local");
            }

            // determine whether it's valid
            if (batchSyncTarget.isEmpty()) {
                batchSyncTarget = currSyncTarget;
            } else if (batchSyncTarget != currSyncTarget) {
                qWarning() << "Error: contacts from multiple sync targets specified in single batch save!";
                return QContactManager::UnspecifiedError;
            }
        }
    }

    if (!withinTransaction && !beginTransaction()) {
        // only create a transaction if we're not within one already
        qWarning() << "Unable to begin database transaction while saving contacts";
        return QContactManager::UnspecifiedError;
    }

    // Find the maximum possible aggregate contact id.
    // This assumes that no two contacts from the same synctarget should be aggregated together.
    if (!m_findMaximumContactId.exec() || !m_findMaximumContactId.next()) {
        qWarning() << "Failed to find max possible aggregate during batch save:" << m_findMaximumContactId.lastError().text();
        return QContactManager::UnspecifiedError;
    }
    int maxAggregateId = m_findMaximumContactId.value(0).toInt();
    m_findMaximumContactId.finish();

    QContactManager::Error worstError = QContactManager::NoError;
    QContactManager::Error err = QContactManager::NoError;
    for (int i = 0; i < contacts->count(); ++i) {
        QContact &contact = (*contacts)[i];
        const QContactIdType contactId = ContactId::apiId(contact);
        bool aggregateUpdated = false;
        if (ContactId::databaseId(contactId) == 0) {
            err = create(&contact, definitionMask, maxAggregateId, true, withinAggregateUpdate);
            if (err == QContactManager::NoError) {
                m_addedIds.insert(ContactId::apiId(contact));
            } else {
                qWarning() << "Error creating contact:" << err << "syncTarget:" << contact.detail<QContactSyncTarget>().syncTarget();
            }
        } else {
            err = update(&contact, definitionMask, &aggregateUpdated, true, withinAggregateUpdate);
            if (err == QContactManager::NoError) {
                m_changedIds.insert(contactId);
            } else {
                qWarning() << "Error updating contact" << contactId << ":" << err;
            }
        }
        if (aggregatesUpdated) {
            aggregatesUpdated->insert(i, aggregateUpdated);
        }
        if (err != QContactManager::NoError) {
            worstError = err;
            if (errorMap) {
                errorMap->insert(i, err);
            }
        }
    }

    if (!withinTransaction) {
        // only attempt to commit/rollback the transaction if we created it
        if (worstError != QContactManager::NoError) {
            // If anything failed at all, we need to rollback, so that we do not
            // have an inconsistent state between aggregate and constituent contacts

            // Any contacts we 'added' are not actually added - clear their IDs
            for (int i = 0; i < contacts->count(); ++i) {
                QContact &contact = (*contacts)[i];
                const QContactIdType contactId = ContactId::apiId(contact);
                if (m_addedIds.contains(contactId)) {
                    contact.setId(QContactId());
                    if (errorMap) {
                        // We also need to report an error for this contact, even though there
                        // is no true error preventing it from being updated
                        errorMap->insert(i, QContactManager::LockedError);
                    }
                }
            }

            rollbackTransaction();
            return worstError;
        } else if (!commitTransaction()) {
            qWarning() << "Failed to commit contacts";
            return QContactManager::UnspecifiedError;
        }
    }

    return worstError;
}

template <typename T> void appendDetailType(ContactWriter::DetailList *list)
{
#ifdef USING_QTPIM
    list->append(T::Type);
#else
    list->append(T::DefinitionName);
#endif
}

static ContactWriter::DetailList allSupportedDetails()
{
    ContactWriter::DetailList details;

    appendDetailType<QContactType>(&details);
    appendDetailType<QContactDisplayLabel>(&details);
    appendDetailType<QContactName>(&details);
    appendDetailType<QContactSyncTarget>(&details);
    appendDetailType<QContactGuid>(&details);
    appendDetailType<QContactNickname>(&details);
    appendDetailType<QContactFavorite>(&details);
    appendDetailType<QContactGender>(&details);
    appendDetailType<QContactTimestamp>(&details);
    appendDetailType<QContactPhoneNumber>(&details);
    appendDetailType<QContactEmailAddress>(&details);
    appendDetailType<QContactBirthday>(&details);
    appendDetailType<QContactAvatar>(&details);
    appendDetailType<QContactOnlineAccount>(&details);
    appendDetailType<QContactPresence>(&details);
    appendDetailType<QContactGlobalPresence>(&details);
    appendDetailType<QContactOriginMetadata>(&details);
    appendDetailType<QContactAddress>(&details);
    appendDetailType<QContactTag>(&details);
    appendDetailType<QContactUrl>(&details);
    appendDetailType<QContactAnniversary>(&details);
    appendDetailType<QContactHobby>(&details);
    appendDetailType<QContactNote>(&details);
    appendDetailType<QContactOrganization>(&details);
    appendDetailType<QContactRingtone>(&details);
    appendDetailType<QContactStatusFlags>(&details);

    return details;
}

static ContactWriter::DetailList allSingularDetails()
{
    ContactWriter::DetailList details;

    appendDetailType<QContactDisplayLabel>(&details);
    appendDetailType<QContactName>(&details);
    appendDetailType<QContactSyncTarget>(&details);
    appendDetailType<QContactGuid>(&details);
    appendDetailType<QContactFavorite>(&details);
    appendDetailType<QContactGender>(&details);
    appendDetailType<QContactTimestamp>(&details);
    appendDetailType<QContactBirthday>(&details);
    appendDetailType<QContactOriginMetadata>(&details);
    appendDetailType<QContactStatusFlags>(&details);

    return details;
}

static QContactManager::Error enforceDetailConstraints(QContact *contact)
{
    static const ContactWriter::DetailList supported(allSupportedDetails());
    static const ContactWriter::DetailList singular(allSingularDetails());

    QHash<ContactWriter::DetailList::value_type, int> detailCounts;

    // look for unsupported detail data.  XXX TODO: this is really slow, due to string comparison.
    // We could simply ignore all unsupported data during save, which would save quite some time.
    foreach (const QContactDetail &det, contact->details()) {
        QString typeName(detailTypeName(det));
        if (!detailListContains(supported, det)) {
            qWarning() << "Invalid detail type:" << typeName;
            return QContactManager::InvalidDetailError;
        } else {
            ++detailCounts[detailType(det)];
        }
    }

    // enforce uniqueness constraints
    foreach (const ContactWriter::DetailList::value_type &type, singular) {
        if (detailCounts[type] > 1) {
            qWarning() << "Invalid count of detail type:" << detailTypeName(type) << ":" << detailCounts[type];
            return QContactManager::LimitReachedError;
        }
    }

    return QContactManager::NoError;
}

/*
    This function is called as part of the "save updated aggregate"
    codepath.  It calculates the list of details which were modified
    in the updated version (compared to the current database version)
    and returns it.
*/
static QContactManager::Error calculateDelta(ContactReader *reader, QContact *contact, const ContactWriter::DetailList &definitionMask, QList<QContactDetail> *addDelta, QList<QContactDetail> *removeDelta)
{
    QContactFetchHint hint;
#ifdef USING_QTPIM
    hint.setDetailTypesHint(definitionMask);
#else
    hint.setDetailDefinitionsHint(definitionMask);
#endif
    hint.setOptimizationHints(QContactFetchHint::NoRelationships);

    QList<QContactIdType> whichList;
    whichList.append(ContactId::apiId(*contact));

    QList<QContact> readList;
    QContactManager::Error readError = reader->readContacts(QLatin1String("UpdateAggregate"), &readList, whichList, hint);
    if (readError != QContactManager::NoError || readList.size() == 0) {
        // unable to read the aggregate contact from the database
        return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
    }

    // Make mutable copies of the contacts (without Irremovable|Readonly flags)
    QContact dbContact;
    foreach (const QContactDetail &detail, readList.at(0).details()) {
        if (detailType(detail) != detailType<QContactDisplayLabel>() &&
            detailType(detail) != detailType<QContactType>() &&
            (definitionMask.isEmpty() || detailListContains(definitionMask, detail))) {
            QContactDetail copy(detail);
            QContactManagerEngine::setDetailAccessConstraints(&copy, QContactDetail::NoConstraint);
            dbContact.saveDetail(&copy);
        }
    }

    QContact upContact;
    foreach (const QContactDetail &detail, contact->details()) {
        if (detailType(detail) != detailType<QContactDisplayLabel>() &&
            detailType(detail) != detailType<QContactType>() &&
            (definitionMask.isEmpty() || detailListContains(definitionMask, detail))) {
            QContactDetail copy(detail);
            QContactManagerEngine::setDetailAccessConstraints(&copy, QContactDetail::NoConstraint);
            upContact.saveDetail(&copy);
        }
    }

    // Determine which details are in the update contact which aren't in the database contact:
    // Detail order is not defined, so loop over the entire set for each, removing matches or
    // superset details (eg, backend added a field (like lastModified to timestamp) on previous save)
    foreach (QContactDetail ddb, dbContact.details()) {
        foreach (QContactDetail dup, upContact.details()) {
            if (detailsSuperset(ddb, dup)) {
                dbContact.removeDetail(&ddb);
                upContact.removeDetail(&dup);
                break;
            }
        }
    }

    // These are details which exist in the updated contact, but not in the database contact.
    addDelta->append(upContact.details());

    // These are details which exist in the database contact, but not in the updated contact.
    removeDelta->append(dbContact.details());

    return QContactManager::NoError;
}

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
static void adjustDetailUrisForLocal(QContactDetail &currDet)
{
    if (currDet.detailUri().startsWith(QLatin1String("aggregate:"))) {
        currDet.setDetailUri(currDet.detailUri().mid(10));
    }

    bool needsLinkedDUs = false;
    QStringList linkedDUs = currDet.linkedDetailUris();
    for (int i = 0; i < linkedDUs.size(); ++i) {
        QString currLDU = linkedDUs.at(i);
        if (currLDU.startsWith(QLatin1String("aggregate"))) {
            currLDU = currLDU.mid(10);
            linkedDUs.replace(i, currLDU);
            needsLinkedDUs = true;
        }
    }
    if (needsLinkedDUs) {
        currDet.setLinkedDetailUris(linkedDUs);
    }
}

/*
    This function is called when an aggregate contact is updated directly.
    The addDelta and remDelta are calculated from the existing (database) aggregate,
    and then these deltas are applied (within this function) to the local contact.
*/
static void promoteDetailsToLocal(const QList<QContactDetail> addDelta, const QList<QContactDetail> remDelta, QContact *localContact, const ContactWriter::DetailList &definitionMask)
{
    // first apply the removals.  Note that these may not always apply
    // (eg, if the client attempted to manually remove a detail which
    // comes from a synced contact, rather than the local contact) -
    // in which case, it'll be ignored.
    QList<QContactDetail> notPresentInLocal;
    foreach (const QContactDetail &det, remDelta) {
        if (detailType(det) == detailType<QContactGuid>() ||
            detailType(det) == detailType<QContactSyncTarget>() ||
            detailType(det) == detailType<QContactDisplayLabel>() ||
            detailType(det) == detailType<QContactStatusFlags>() ||
            (!definitionMask.isEmpty() && !detailListContains(definitionMask, det))) {
            continue; // don't remove these details.  They cannot apply to the local contact.
        }

        // handle unique details specifically.
        QContactDetail detToRemove;
        if ((detailType(det) == detailType<QContactName>()) ||
            (detailType(det) == detailType<QContactTimestamp>()) ||
            (detailType(det) == detailType<QContactGender>()) ||
            (detailType(det) == detailType<QContactFavorite>()) ||
            (detailType(det) == detailType<QContactBirthday>()) ||
            (detailType(det) == detailType<QContactStatusFlags>())) {
            detToRemove = localContact->detail(detailType(det));
            localContact->removeDetail(&detToRemove);
        } else {
            // all other details are just removed directly.
            bool found = false;
            QList<QContactDetail> allDets = localContact->details();
            for (int j = 0; j < allDets.size(); ++j) {
                detToRemove = allDets.at(j);
                if (detailsEquivalent(detToRemove, det)) {
                    // note: this comparison does value checking only.
                    localContact->removeDetail(&detToRemove);
                    found = true;
                    break;
                }
            }

            if (!found) {
                notPresentInLocal.append(det);
            }
        }
    }

#if 0
    // debugging: print out some information about the removals which were ignored / not applied.
    QString ignoredStr;
    foreach (const QContactDetail &det, notPresentInLocal) {
        QString valStr;
        if (det.definitionName() == QContactName::DefinitionName) {
            valStr = det.value(QContactName::FieldFirstName) + QLatin1String(" ") + det.value(QContactName::FieldLastName);
        } else if (det.definitionName() == QContactPhoneNumber::DefinitionName) {
            valStr = det.value(QContactPhoneNumber::FieldNumber);
        } else if (det.definitionName() == QContactEmailAddress::DefinitionName) {
            valStr = det.value(QContactEmailAddress::FieldEmailAddress);
        } else {
            valStr = QLatin1String("Some value...");
        }
        ignoredStr += QString(QLatin1String("\n    %1: %2")).arg(det.definitionName()).arg(valStr);
    }
    qWarning() << "promoteDetailsToLocal: IGNORED" << notPresentInLocal.size() << "details:" << ignoredStr;
#endif

    foreach (QContactDetail det, addDelta) {
        if (detailType(det) == detailType<QContactGuid>() ||
            detailType(det) == detailType<QContactSyncTarget>() ||
            detailType(det) == detailType<QContactDisplayLabel>() ||
            detailType(det) == detailType<QContactStatusFlags>() ||
            (!definitionMask.isEmpty() && !detailListContains(definitionMask, det))) {
            continue; // don't save these details.  Guid MUST be globally unique.
        }

        // handle unique details specifically.
        if (detailType(det) == detailType<QContactName>()) {
            QContactName lcn = localContact->detail<QContactName>();
            lcn.setPrefix(det.value<QString>(QContactName::FieldPrefix));
            lcn.setFirstName(det.value<QString>(QContactName::FieldFirstName));
            lcn.setMiddleName(det.value<QString>(QContactName::FieldMiddleName));
            lcn.setLastName(det.value<QString>(QContactName::FieldLastName));
            lcn.setSuffix(det.value<QString>(QContactName::FieldSuffix));
#ifdef USING_QTPIM
            lcn.setValue(QContactName__FieldCustomLabel, det.value(QContactName__FieldCustomLabel));
#else
            lcn.setCustomLabel(det.value<QString>(QContactName::FieldCustomLabel));
#endif
            localContact->saveDetail(&lcn);
        } else if (detailType(det) == detailType<QContactTimestamp>()) {
            QContactTimestamp lts = localContact->detail<QContactTimestamp>();
            lts.setLastModified(det.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp));
            lts.setCreated(det.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp));
            localContact->saveDetail(&lts);
        } else if (detailType(det) == detailType<QContactGender>()) {
            QContactGender lg = localContact->detail<QContactGender>();
#ifdef USING_QTPIM
            // Gender is a string in QtMobility
            QString gender(det.value<QString>(QContactGender::FieldGender));
            if (gender.startsWith(QChar::fromLatin1('f'), Qt::CaseInsensitive)) {
                lg.setGender(QContactGender::GenderFemale);
            } else if (gender.startsWith(QChar::fromLatin1('m'), Qt::CaseInsensitive)) {
                lg.setGender(QContactGender::GenderMale);
            } else {
                lg.setGender(QContactGender::GenderUnspecified);
            }
#else
            lg.setGender(det.value<QString>(QContactGender::FieldGender));
#endif
            localContact->saveDetail(&lg);
        } else if (detailType(det) == detailType<QContactFavorite>()) {
            QContactFavorite lf = localContact->detail<QContactFavorite>();
            lf.setFavorite(det.value<bool>(QContactFavorite::FieldFavorite));
            localContact->saveDetail(&lf);
        } else if (detailType(det) == detailType<QContactBirthday>()) {
            QContactBirthday bd = localContact->detail<QContactBirthday>();
            bd.setDateTime(det.value<QDateTime>(QContactBirthday::FieldBirthday));
            localContact->saveDetail(&bd);
        } else {
            // other details can be saved to the local contact (if they don't already exist).
            adjustDetailUrisForLocal(det);

            // This is a pretty crude heuristic.  The detail equality
            // algorithm only attempts to match values, not key/value pairs.
            // XXX TODO: use a better heuristic to minimise duplicates.
            bool needsPromote = true;

            // Don't promote details already in the local, or those not originally present in the local
            QList<QContactDetail> noPromoteDetails(localContact->details() + notPresentInLocal);
            foreach (QContactDetail ld, noPromoteDetails) {
                if (detailsEquivalent(det, ld)) {
                    needsPromote = false;
                    break;
                }
            }

            if (needsPromote) {
                localContact->saveDetail(&det);
            }
        }
    }
}
#endif

static QContactRelationship makeRelationship(const QString &type, const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship;
    relationship.setRelationshipType(type);

#ifdef USING_QTPIM
    QContact first, second;
    first.setId(firstId);
    second.setId(secondId);
    relationship.setFirst(first);
    relationship.setSecond(second);
#else
    relationship.setFirst(firstId);
    relationship.setSecond(secondId);
#endif

    return relationship;
}

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
/*
   This function is called when an aggregate contact is updated.
   Instead of just saving changes to the aggregate, we save the
   changes (delta) to the 'local' contact (creating one if necessary)
   and promote all changes to the aggregate.
*/
QContactManager::Error ContactWriter::updateLocalAndAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction)
{
    // 1) calculate the delta between "the new aggregate contact" and the "database aggregate contact"; bail out if no changes.
    // 2) get the contact which is aggregated by the aggregate contact and has a 'local' sync target
    // 3) if it exists, go to (5)
    // 4) create a new contact with 'local' sync target and the name copied from the aggregate
    // 5) save the "delta details" into the local sync target
    // 6) clobber the database aggregate by overwriting with the new aggregate.

    QList<QContactDetail> addDeltaDetails;
    QList<QContactDetail> remDeltaDetails;
    QContactManager::Error deltaError = calculateDelta(m_reader, contact, definitionMask, &addDeltaDetails, &remDeltaDetails);
    if (deltaError != QContactManager::NoError) {
        qWarning() << "Unable to calculate delta for modified aggregate";
        return deltaError;
    }

    if (addDeltaDetails.isEmpty() && remDeltaDetails.isEmpty()) {
        return QContactManager::NoError; // nothing to do.
    }

    m_findLocalForAggregate.bindValue(":aggregateId", ContactId::databaseId(contact->id()));
    if (!m_findLocalForAggregate.exec()) {
        qWarning() << "Unable to query local for aggregate during update";
        qWarning() << m_findLocalForAggregate.lastError();
        return QContactManager::UnspecifiedError;
    }

    bool createdNewLocal = false;
    QContact localContact;
    if (m_findLocalForAggregate.next()) {
        // found the existing local contact aggregated by this aggregate.
        QList<QContactIdType> whichList;
        whichList.append(ContactId::apiId(m_findLocalForAggregate.value(0).toUInt()));

        m_findLocalForAggregate.finish();

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("UpdateAggregate"), &readList, whichList, hint);
        if (readError != QContactManager::NoError || readList.size() == 0) {
            qWarning() << "Unable to read local contact for aggregate" << ContactId::toString(*contact) << "during update";
            return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
        }

        localContact = readList.at(0);
    } else {
        m_findLocalForAggregate.finish();

        // no local contact exists for the aggregate.  Create a new one.
        createdNewLocal = true;

        QContactSyncTarget lst;
        lst.setSyncTarget(QLatin1String("local"));
        localContact.saveDetail(&lst);

        QContactName lcn = contact->detail<QContactName>();
        adjustDetailUrisForLocal(lcn);
        localContact.saveDetail(&lcn);
    }

    // promote delta to local contact
    promoteDetailsToLocal(addDeltaDetails, remDeltaDetails, &localContact, definitionMask);

    // update (or create) the local contact
    QMap<int, bool> aggregatesUpdated;
    QMap<int, QContactManager::Error> errorMap;
    QList<QContact> writeList;
    writeList.append(localContact);
    QContactManager::Error writeError = save(&writeList, DetailList(),     // when we update the local, we don't use definitionMask.
                                             &aggregatesUpdated, &errorMap,  // because it might be a new contact and need name+synct.
                                             withinTransaction, true);
    if (writeError != QContactManager::NoError) {
        qWarning() << "Unable to update (or create) local contact for modified aggregate";
        return writeError;
    }

    if (createdNewLocal) {
        // Add the aggregates relationship
        QList<QContactRelationship> saveRelationshipList;
        saveRelationshipList.append(makeRelationship(relationshipString(QContactRelationship::Aggregates), contact->id(), writeList.at(0).id()));
        writeError = save(saveRelationshipList, &errorMap, withinTransaction);
        if (writeError != QContactManager::NoError) {
            // TODO: remove unaggregated contact
            // if the aggregation relationship fails, the entire save has failed.
            qWarning() << "Unable to save aggregation relationship for new local contact!";
            return writeError;
        }
    }

    if (aggregatesUpdated.count() && (aggregatesUpdated[0] == true)) {
        // Saving the local has caused the aggregate to be regenerated and saved, so we
        // don't need to save it now (our copy doesn't have the regenerated details yet)
    } else {
        // update (via clobber) the aggregate contact
        errorMap.clear();
        writeList.clear();
        writeList.append(*contact);
        writeError = save(&writeList, definitionMask, 0, &errorMap, withinTransaction, true); // we're updating the aggregate contact deliberately.
        if (writeError != QContactManager::NoError) {
            qWarning() << "Unable to update modified aggregate";
            if (createdNewLocal) {
                QList<QContactIdType> removeList;
                removeList.append(ContactId::apiId(localContact));
                QContactManager::Error removeError = remove(removeList, &errorMap, withinTransaction);
                if (removeError != QContactManager::NoError) {
                    qWarning() << "Unable to remove stale local contact created for modified aggregate";
                }
            }
        }
    }

    return writeError;
}

static void adjustDetailUrisForAggregate(QContactDetail &currDet)
{
    if (!currDet.detailUri().isEmpty()) {
        currDet.setDetailUri(QLatin1String("aggregate:") + currDet.detailUri());
    }

    bool needsLinkedDUs = false;
    QStringList linkedDUs = currDet.linkedDetailUris();
    for (int i = 0; i < linkedDUs.size(); ++i) {
        QString currLDU = linkedDUs.at(i);
        if (!currLDU.isEmpty()) {
            currLDU = QLatin1String("aggregate:") + currLDU;
            linkedDUs.replace(i, currLDU);
            needsLinkedDUs = true;
        }
    }
    if (needsLinkedDUs) {
        currDet.setLinkedDetailUris(linkedDUs);
    }
}

static ContactWriter::DetailList getIdentityDetailTypes()
{
    // The list of definition names for details that identify a contact
    ContactWriter::DetailList rv;
    rv << detailType<QContactSyncTarget>()
       << detailType<QContactGuid>()
       << detailType<QContactType>();
    return rv;
}

static ContactWriter::DetailList getUnpromotedDetailTypes()
{
    // The list of definition names for details that are not propagated to an aggregate
    ContactWriter::DetailList rv(getIdentityDetailTypes());
    rv << detailType<QContactDisplayLabel>();
    rv << detailType<QContactGlobalPresence>();
    rv << detailType<QContactStatusFlags>();
    return rv;
}

/*
    For every detail in a contact \a c, this function will check to see if an
    identical detail already exists in the \a aggregate contact.  If not, the
    detail from \a c will be "promoted" (saved in) the \a aggregate contact.

    Note that QContactSyncTarget and QContactGuid details will NOT be promoted,
    nor will QContactDisplayLabel or QContactType details.
*/
static void promoteDetailsToAggregate(const QContact &contact, QContact *aggregate, const ContactWriter::DetailList &definitionMask)
{
    static const ContactWriter::DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());

    QList<QContactDetail> currDetails = contact.details();
    for (int j = 0; j < currDetails.size(); ++j) {
        QContactDetail currDet = currDetails.at(j);
        if (unpromotedDetailTypes.contains(detailType(currDet))) {
            // don't promote this detail.
            continue;
        }
        if (!definitionMask.isEmpty() && !detailListContains(definitionMask, currDet)) {
            // skip this detail
            continue;
        }

        // promote this detail to the aggregate.  Depending on uniqueness,
        // this consists either of composition or duplication.
        // Note: Composed (unique) details won't have any detailUri!
        if (detailType(currDet) == detailType<QContactName>()) {
            // name involves composition
            QContactName cname(currDet);
            QContactName aname(aggregate->detail<QContactName>());
            if (!cname.prefix().isEmpty() && aname.prefix().isEmpty())
                aname.setPrefix(cname.prefix());
            if (!cname.firstName().isEmpty() && aname.firstName().isEmpty())
                aname.setFirstName(cname.firstName());
            if (!cname.middleName().isEmpty() && aname.middleName().isEmpty())
                aname.setMiddleName(cname.middleName());
            if (!cname.lastName().isEmpty() && aname.lastName().isEmpty())
                aname.setLastName(cname.lastName());
            if (!cname.suffix().isEmpty() && aname.suffix().isEmpty())
                aname.setSuffix(cname.suffix());
#ifdef USING_QTPIM
            QString customLabel = cname.value<QString>(QContactName__FieldCustomLabel);
            if (!customLabel.isEmpty() && aname.value<QString>(QContactName__FieldCustomLabel).isEmpty())
                aname.setValue(QContactName__FieldCustomLabel, cname.value(QContactName__FieldCustomLabel));
#else
            if (!cname.customLabel().isEmpty() && aname.customLabel().isEmpty())
                aname.setCustomLabel(cname.customLabel());
#endif
            aggregate->saveDetail(&aname);
        } else if (detailType(currDet) == detailType<QContactTimestamp>()) {
            // timestamp involves composition
            // XXX TODO: how do we handle creation timestamps?
            // From some sync sources, the creation timestamp
            // will precede the existence of the local device.
            QContactTimestamp cts(currDet);
            QContactTimestamp ats(aggregate->detail<QContactTimestamp>());
            if (cts.lastModified().isValid() && (!ats.lastModified().isValid() || cts.lastModified() > ats.lastModified())) {
                ats.setLastModified(cts.lastModified());
                aggregate->saveDetail(&ats);
            }
        } else if (detailType(currDet) == detailType<QContactGender>()) {
            // gender involves composition
            QContactGender cg(currDet);
            QContactGender ag(aggregate->detail<QContactGender>());
#ifdef USING_QTPIM
            if (!cg.gender() != QContactGender::GenderUnspecified && ag.gender() == QContactGender::GenderUnspecified) {
#else
            if (!cg.gender().isEmpty() && ag.gender().isEmpty()) {
#endif
                ag.setGender(cg.gender());
                aggregate->saveDetail(&ag);
            }
        } else if (detailType(currDet) == detailType<QContactFavorite>()) {
            // favorite involves composition
            QContactFavorite cf(currDet);
            QContactFavorite af(aggregate->detail<QContactFavorite>());
            if (cf.isFavorite() && !af.isFavorite()) {
                af.setFavorite(true);
                aggregate->saveDetail(&af);
            }
        } else if (detailType(currDet) == detailType<QContactBirthday>()) {
            // birthday involves composition (at least, it's unique)
            QContactBirthday cb(currDet);
            QContactBirthday ab(aggregate->detail<QContactBirthday>());
            if (!ab.dateTime().isValid() && cb.dateTime().isValid()) {
                ab.setDateTime(cb.dateTime());
                aggregate->saveDetail(&ab);
            }
        } else {
            // All other details involve duplication.
            // Only duplicate from contact to the aggregate if an identical detail doesn't already exist in the aggregate.
            // We also modify any detail uris by prepending "aggregate:" to the start,
            // to ensure uniqueness.
            adjustDetailUrisForAggregate(currDet);

            // This is a pretty crude heuristic.  The detail equality
            // algorithm only attempts to match values, not key/value pairs.
            // XXX TODO: use a better heuristic to minimise duplicates.
            bool needsPromote = true;
            QList<QContactDetail> allADetails = aggregate->details();
            foreach (QContactDetail ad, allADetails) {
                if (detailsEquivalent(currDet, ad)) {
                    needsPromote = false;
                    break;
                }
            }

            if (needsPromote) {
                QString syncTarget(contact.detail<QContactSyncTarget>().value<QString>(QContactSyncTarget::FieldSyncTarget));
                if (!syncTarget.isEmpty() && syncTarget != QLatin1String("local")) {
                    QContactManagerEngine::setDetailAccessConstraints(&currDet, QContactDetail::ReadOnly | QContactDetail::Irremovable);
                }
                aggregate->saveDetail(&currDet);
            }
        }
    }
}

/*
   This function is called when a new contact is created.  The
   aggregate contacts are searched for a match, and the matching
   one updated if it exists; or a new aggregate is created.
*/
QContactManager::Error ContactWriter::updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, int maxAggregateId, bool withinTransaction)
{
    // 1) search for match
    // 2) if exists, update the existing aggregate (by default, non-clobber:
    //    only update empty fields of details, or promote non-existent details.  Never delete or replace details.)
    // 3) otherwise, create new aggregate, consisting of all details of contact, return.

    QMap<int, QContactManager::Error> errorMap;
    QList<QContact> saveContactList;
    QList<QContactRelationship> saveRelationshipList;

    QString firstName;
    QString lastName;
    QString nickname;
    QStringList phoneNumbers;
    QStringList emailAddresses;
    QStringList accountUris;
    QString syncTarget;

    quint32 contactId = ContactId::databaseId(*contact);
    foreach (const QContactName &detail, contact->details<QContactName>()) {
        firstName = detail.firstName().toLower();
        lastName = detail.lastName().toLower();
        break;
    }
    foreach (const QContactNickname &detail, contact->details<QContactNickname>()) {
        nickname = detail.nickname().toLower();
        break;
    }
    foreach (const QContactPhoneNumber &detail, contact->details<QContactPhoneNumber>()) {
        phoneNumbers.append(ContactsEngine::normalizedPhoneNumber(detail.number()));
    }
    foreach (const QContactEmailAddress &detail, contact->details<QContactEmailAddress>()) {
        emailAddresses.append(detail.emailAddress().toLower());
    }
    foreach (const QContactOnlineAccount &detail, contact->details<QContactOnlineAccount>()) {
        accountUris.append(detail.accountUri().toLower());
    }
    syncTarget = contact->detail<QContactSyncTarget>().syncTarget();
    if (syncTarget.isEmpty()) {
        syncTarget = QLatin1String("local");
    }

    static const QLatin1Char Percent('%');

    // Use a simple match algorithm, looking for exact matches on name fields,
    // or accumulating points for name matches (including partial matches of first name).

    m_findMatchForContact.bindValue(":id", contactId);
    m_findMatchForContact.bindValue(":firstName", firstName);
    m_findMatchForContact.bindValue(":firstPartial", firstName.mid(0, 4).prepend(Percent).append(Percent));
    m_findMatchForContact.bindValue(":lastName", lastName);
    m_findMatchForContact.bindValue(":nickname", nickname);
    m_findMatchForContact.bindValue(":number", phoneNumbers.join(","));
    m_findMatchForContact.bindValue(":email", emailAddresses.join(","));
    m_findMatchForContact.bindValue(":uri", accountUris.join(","));
    m_findMatchForContact.bindValue(":maxAggregateId", maxAggregateId);
    m_findMatchForContact.bindValue(":syncTarget", syncTarget);

    QContact matchingAggregate;
    bool found = false;

    if (!m_findMatchForContact.exec()) {
        qWarning() << m_findMatchForContact.lastError();
        return QContactManager::UnspecifiedError;
    }
    if (m_findMatchForContact.next()) {
        QContactIdType aggregateId = ContactId::apiId(m_findMatchForContact.value(0).toUInt());
        quint32 score = m_findMatchForContact.value(1).toUInt();
        m_findMatchForContact.finish();

        static const quint32 MinimumMatchScore = 15;
        if (score >= MinimumMatchScore) {
            QList<QContactIdType> readIds;
            readIds.append(aggregateId);

            QContactFetchHint hint;
            hint.setOptimizationHints(QContactFetchHint::NoRelationships);

            QList<QContact> readList;
            QContactManager::Error readError = m_reader->readContacts(QLatin1String("CreateAggregate"), &readList, readIds, hint);
            if (readError != QContactManager::NoError || readList.size() < 1) {
                qWarning() << "Failed to read aggregate contact" << aggregateId << "during regenerate";
                return QContactManager::UnspecifiedError;
            }

            matchingAggregate = readList.at(0);
            found = true;
        }
    }
    m_findMatchForContact.finish();

    // whether it's an existing or new contact, we promote details.
    // XXX TODO: promote relationships!
    promoteDetailsToAggregate(*contact, &matchingAggregate, definitionMask);
    if (!found) {
        // need to create an aggregating contact first.
        QContactSyncTarget cst;
        cst.setSyncTarget(QLatin1String("aggregate"));
        matchingAggregate.saveDetail(&cst);
    }

    // now save in database.
    saveContactList.append(matchingAggregate);
    QContactManager::Error err = save(&saveContactList, DetailList(), 0, &errorMap, withinTransaction, true); // we're updating (or creating) the aggregate
    if (err != QContactManager::NoError) {
        if (!found) {
            qWarning() << "Could not create new aggregate contact";
        } else {
            qWarning() << "Could not update existing aggregate contact";
        }
        return err;
    }
    matchingAggregate = saveContactList.at(0);

    // add the relationship and save in the database.
    // Note: we DON'T use the existing save(relationshipList, ...) function
    // as it does (expensive) aggregate regeneration which we have already
    // done above (via the detail promotion and aggregate save).
    // Instead, we simply add the "aggregates" relationship directly.
    m_insertRelationship.bindValue(":firstId", ContactId::databaseId(matchingAggregate));
    m_insertRelationship.bindValue(":secondId", ContactId::databaseId(*contact));
    m_insertRelationship.bindValue(":type", relationshipString(QContactRelationship::Aggregates));
    if (!m_insertRelationship.exec()) {
        qWarning() << "error inserting Aggregates relationship: " << m_insertRelationship.lastError();
        err = QContactManager::UnspecifiedError;
    }

    if (err != QContactManager::NoError) {
        // if the aggregation relationship fails, the entire save has failed.
        qWarning() << "Unable to save aggregation relationship!";

        if (!found) {
            // clean up the newly created contact.
            QList<QContactIdType> removeList;
            removeList.append(ContactId::apiId(matchingAggregate));
            QContactManager::Error cleanupErr = remove(removeList, &errorMap, withinTransaction);
            if (cleanupErr != QContactManager::NoError) {
                qWarning() << "Unable to cleanup newly created aggregate contact!";
            }
        }
    }

    return err;
}

/*
    This function is called as part of the "remove contacts" codepath.
    Any aggregate contacts which still exist after the remove operation
    which used to aggregate a contact which was removed during the operation
    needs to be regenerated (as some details may no longer be valid).

    If the operation fails, it's not a huge issue - we don't need to rollback
    the database.  It simply means that the existing aggregates may contain
    some stale data.
*/
void ContactWriter::regenerateAggregates(const QList<quint32> &aggregateIds, const DetailList &definitionMask, bool withinTransaction)
{
    static const DetailList identityDetailTypes(getIdentityDetailTypes());
    static const DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());

    // for each aggregate contact:
    // 1) get the contacts it aggregates
    // 2) build unique details via composition (name / timestamp / gender / favorite - NOT synctarget or guid)
    // 3) append non-unique details
    // In all cases, we "prefer" the 'local' contact's data (if it exists)

    QList<QContact> aggregatesToSave;
    QSet<QContactIdType> aggregatesToSaveIds;
    foreach (quint32 aggId, aggregateIds) {
        QContactIdType apiId(ContactId::apiId(aggId));
        if (aggregatesToSaveIds.contains(apiId)) {
            continue;
        }

        QList<QContactIdType> readIds;
        readIds.append(apiId);

        m_findConstituentsForAggregate.bindValue(":aggregateId", aggId);
        if (!m_findConstituentsForAggregate.exec()) {
            qWarning() << "Failed to find constituent contacts for aggregate" << aggId << "during regenerate";
            continue;
        }
        while (m_findConstituentsForAggregate.next()) {
            readIds.append(ContactId::apiId(m_findConstituentsForAggregate.value(0).toUInt()));
        }
        m_findConstituentsForAggregate.finish();

        if (readIds.size() == 1) { // only the aggregate?
            qWarning() << "Existing aggregate" << aggId << "should already have been removed - aborting regenerate";
            continue;
        }

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("RegenerateAggregate"), &readList, readIds, hint);
        if (readError != QContactManager::NoError
                || readList.size() <= 1
                || readList.at(0).detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != QLatin1String("aggregate")) {
            qWarning() << "Failed to read constituent contacts for aggregate" << aggId << "during regenerate";
            continue;
        }

        QContact originalAggregateContact = readList.at(0);

        QContact aggregateContact;
        aggregateContact.setId(originalAggregateContact.id());

        // Copy any existing fields not affected by this update
        foreach (const QContactDetail &detail, originalAggregateContact.details()) {
            if (detailListContains(identityDetailTypes, detail) ||
                (!definitionMask.isEmpty() &&
                 !detailListContains(definitionMask, detail) &&
                 !detailListContains(unpromotedDetailTypes, detail))) {
                // Copy this detail to the new aggregate
                QContactDetail newDetail(detail);
                if (!aggregateContact.saveDetail(&newDetail)) {
                    qWarning() << "Contact:" << ContactId::toString(aggregateContact) << "Failed to copy existing detail:" << detail;
                }
            }
        }

        // Step two: search for the "local" contact and promote its details first
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != QLatin1String("local"))
                continue;
            QList<QContactDetail> currDetails = curr.details();
            for (int j = 0; j < currDetails.size(); ++j) {
                QContactDetail currDet = currDetails.at(j);
                if (!detailListContains(unpromotedDetailTypes, currDet) &&
                    (definitionMask.isEmpty() || detailListContains(definitionMask, currDet))) {
                    // promote this detail to the aggregate.
                    adjustDetailUrisForAggregate(currDet);
                    aggregateContact.saveDetail(&currDet);
                }
            }
            break; // we've successfully promoted the local contact's details to the aggregate.
        }

        // Step Three: promote data from details of other related contacts
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) == QLatin1String("local")) {
                continue; // already promoted the "local" contact's details.
            }

            // need to promote this contact's details to the aggregate
            promoteDetailsToAggregate(curr, &aggregateContact, definitionMask);
        }

        // we save the updated aggregates to database all in a batch at the end.
        aggregatesToSave.append(aggregateContact);
        aggregatesToSaveIds.insert(ContactId::apiId(aggregateContact));
    }

    QMap<int, QContactManager::Error> errorMap;
    QContactManager::Error writeError = save(&aggregatesToSave, DetailList(), 0, &errorMap, withinTransaction, true); // we're updating aggregates.
    if (writeError != QContactManager::NoError) {
        qWarning() << "Failed to write updated aggregate contacts during regenerate";
        qWarning() << "definitionMask:" << definitionMask;
    }
}

QContactManager::Error ContactWriter::removeChildlessAggregates(QList<QContactIdType> *removedIds)
{
    QVariantList aggregateIds;
    if (!m_childlessAggregateIds.exec()) {
        qWarning() << "Failed to fetch childless aggregate contact ids during remove";
        qWarning() << m_childlessAggregateIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_childlessAggregateIds.next()) {
        quint32 aggregateId = m_childlessAggregateIds.value(0).toUInt();
        aggregateIds.append(aggregateId);
        removedIds->append(ContactId::apiId(aggregateId));
    }
    m_childlessAggregateIds.finish();

    if (aggregateIds.size() > 0) {
        m_removeContact.bindValue(QLatin1String(":contactId"), aggregateIds);
        if (!m_removeContact.execBatch()) {
            qWarning() << "Failed to remove childless aggregate contacts";
            qWarning() << m_removeContact.lastError();
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.finish();
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::aggregateOrphanedContacts(bool withinTransaction)
{
    QList<QContactIdType> contactIds;
    if (!m_orphanContactIds.exec()) {
        qWarning() << "Failed to fetch orphan aggregate contact ids during remove";
        qWarning() << m_orphanContactIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_orphanContactIds.next()) {
        quint32 orphanId = m_orphanContactIds.value(0).toUInt();
        contactIds.append(ContactId::apiId(orphanId));
    }
    m_orphanContactIds.finish();

    if (contactIds.size() > 0) {
        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("AggregateOrphaned"), &readList, contactIds, hint);
        if (readError != QContactManager::NoError || readList.size() != contactIds.size()) {
            qWarning() << "Failed to read orphaned contacts for aggregation";
            return QContactManager::UnspecifiedError;
        }

        if (!m_findMaximumContactId.exec() || !m_findMaximumContactId.next()) {
            qWarning() << "Failed to find max possible aggregate for orphan:" << m_findMaximumContactId.lastError().text();
            return QContactManager::UnspecifiedError;
        }
        int maxAggregateId = m_findMaximumContactId.value(0).toInt();
        m_findMaximumContactId.finish();

        QList<QContact>::iterator it = readList.begin(), end = readList.end();
        for ( ; it != end; ++it) {
            QContact &orphan(*it);
            QContactManager::Error error = updateOrCreateAggregate(&orphan, DetailList(), maxAggregateId, withinTransaction);
            if (error != QContactManager::NoError) {
                qWarning() << "Failed to create aggregate for orphaned contact:" << ContactId::toString(orphan);
                return error;
            }
        }
    }

    return QContactManager::NoError;
}
#endif

static bool updateGlobalPresence(QContact *contact)
{
    QContactGlobalPresence globalPresence = contact->detail<QContactGlobalPresence>();

    const QList<QContactPresence> details = contact->details<QContactPresence>();
    if (details.isEmpty()) {
        // No presence - remove global presence if present
        if (!globalPresence.isEmpty()) {
            contact->removeDetail(&globalPresence);
        }
        return true;
    }

    QContactPresence bestPresence;

    foreach (const QContactPresence &detail, details) {
        if (betterPresence(detail, bestPresence)) {
            bestPresence = detail;
        }
    }

    globalPresence.setPresenceState(bestPresence.presenceState());
    globalPresence.setTimestamp(bestPresence.timestamp());
    globalPresence.setNickname(bestPresence.nickname());
    globalPresence.setCustomMessage(bestPresence.customMessage());

    contact->saveDetail(&globalPresence);
    return true;
}

static bool updateTimestamp(QContact *contact, bool setCreationTimestamp)
{
    QContactTimestamp timestamp = contact->detail<QContactTimestamp>();
    QDateTime createdTime = timestamp.created().toUTC();
    QDateTime modifiedTime = timestamp.lastModified().toUTC();
    bool needsSave = false;

    if (!modifiedTime.isValid()) {
        modifiedTime = QDateTime::currentDateTimeUtc();
        timestamp.setLastModified(modifiedTime);
        needsSave = true;
    }

    if (setCreationTimestamp && !createdTime.isValid()) {
        timestamp.setCreated(modifiedTime);
        needsSave = true;
    }

    if (needsSave) {
        return contact->saveDetail(&timestamp);
    }

    return true;
}

QContactManager::Error ContactWriter::create(QContact *contact, const DetailList &definitionMask, int maxAggregateId, bool withinTransaction, bool withinAggregateUpdate)
{
#ifndef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    Q_UNUSED(withinTransaction)
    Q_UNUSED(withinAggregateUpdate)
#endif
    QContactManager::Error writeErr = enforceDetailConstraints(contact);
    if (writeErr != QContactManager::NoError) {
        qWarning() << "Contact failed detail constraints";
        return writeErr;
    }

    // update the global presence (display label may be derived from it)
    updateGlobalPresence(contact);

    // update the display label for this contact
    m_engine.regenerateDisplayLabel(*contact);

    // update the timestamp if necessary
    updateTimestamp(contact, true); // set creation timestamp

    bindContactDetails(*contact, m_insertContact, DetailList(), false);
    if (!m_insertContact.exec()) {
        qWarning() << "Failed to create contact";
        qWarning() << m_insertContact.lastError();
        return QContactManager::UnspecifiedError;
    }
    quint32 contactId = m_insertContact.lastInsertId().toUInt();
    m_insertContact.finish();

    writeErr = write(contactId, contact, definitionMask);
    if (writeErr == QContactManager::NoError) {
        // successfully saved all data.  Update id.
        contact->setId(ContactId::contactId(ContactId::apiId(contactId)));

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
        if (!withinAggregateUpdate) {
            // and either update the aggregate contact (if it exists) or create a new one (unless it is an aggregate contact).
            if (contact->detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != QLatin1String("aggregate")) {
                writeErr = updateOrCreateAggregate(contact, definitionMask, maxAggregateId, withinTransaction);
            }
        }
#endif
    }

    if (writeErr != QContactManager::NoError) {
        // error occurred.  Remove the failed entry.
        m_removeContact.bindValue(":contactId", contactId);
        if (!m_removeContact.exec()) {
            qWarning() << "Unable to remove stale contact after failed save";
            qWarning() << m_removeContact.lastError().text();
        }
        m_removeContact.finish();
    }

    return writeErr;
}

QContactManager::Error ContactWriter::update(QContact *contact, const DetailList &definitionMask, bool *aggregateUpdated, bool withinTransaction, bool withinAggregateUpdate)
{
#ifndef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    Q_UNUSED(withinTransaction)
    Q_UNUSED(withinAggregateUpdate)
#endif
    *aggregateUpdated = false;

    quint32 contactId = ContactId::databaseId(*contact);

    m_checkContactExists.bindValue(0, contactId);
    if (!m_checkContactExists.exec()) {
        qWarning() << "Failed to check contact existence";
        qWarning() << m_checkContactExists.lastError();
        return QContactManager::UnspecifiedError;
    }
    m_checkContactExists.next();
    int exists = m_checkContactExists.value(0).toInt();
    QString oldSyncTarget = m_checkContactExists.value(1).toString();
    m_checkContactExists.finish();

    if (!exists)
        return QContactManager::DoesNotExistError;

    QString newSyncTarget = contact->detail<QContactSyncTarget>().value<QString>(QContactSyncTarget::FieldSyncTarget);

    if (newSyncTarget != oldSyncTarget &&
        (oldSyncTarget != QLatin1String("local") && oldSyncTarget != QLatin1String("was_local"))) {
        // they are attempting to manually change the sync target value of a non-local contact
        qWarning() << "Cannot manually change sync target!";
        return QContactManager::InvalidDetailError;
    }

    QContactManager::Error writeError = enforceDetailConstraints(contact);
    if (writeError != QContactManager::NoError) {
        qWarning() << "Contact failed detail constraints";
        return writeError;
    }

    // update the modification timestamp
    updateTimestamp(contact, false);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (!withinAggregateUpdate && oldSyncTarget == QLatin1String("aggregate")) {
        // Attempting to update the aggregate contact.
        // We calculate the delta (old contact / new contact)
        // and save the delta to the 'local' contact (might
        // have to create it, if it does not exist).
        // In order to conform to the semantics, we also clobber
        // the aggregate with the current contact's details
        // (ie, not using a heuristic aggregation algorithm).
        return updateLocalAndAggregate(contact, definitionMask, withinTransaction);
    }
#endif

    // update the global presence (display label may be derived from it)
    updateGlobalPresence(contact);

    // update the display label for this contact
    m_engine.regenerateDisplayLabel(*contact);

    bindContactDetails(*contact, m_updateContact, definitionMask, true);
    m_updateContact.bindValue(22, contactId);
    if (!m_updateContact.exec()) {
        qWarning() << "Failed to update contact";
        qWarning() << m_updateContact.lastError();
        return QContactManager::UnspecifiedError;
    }
    m_updateContact.finish();

    writeError = write(contactId, contact, definitionMask);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (writeError == QContactManager::NoError) {
        if (oldSyncTarget != QLatin1String("aggregate")) {
            QList<quint32> aggregatesOfUpdated;
            m_findAggregateForContact.bindValue(":localId", contactId);
            if (!m_findAggregateForContact.exec()) {
                qWarning() << "Failed to fetch aggregator contact ids during remove";
                qWarning() << m_findAggregateForContact.lastError();
                return QContactManager::UnspecifiedError;
            }
            while (m_findAggregateForContact.next()) {
                aggregatesOfUpdated.append(m_findAggregateForContact.value(0).toUInt());
            }
            m_findAggregateForContact.finish();

            if (aggregatesOfUpdated.size() > 0) {
                *aggregateUpdated = true;
                regenerateAggregates(aggregatesOfUpdated, definitionMask, withinTransaction);
            }
        }
    }
#endif

    return writeError;
}

QContactManager::Error ContactWriter::write(quint32 contactId, QContact *contact, const DetailList &definitionMask)
{
    QContactManager::Error error = QContactManager::NoError;
    if (writeDetails<QContactAddress>(contactId, contact, m_removeAddress, definitionMask, &error)
            && writeDetails<QContactAnniversary>(contactId, contact, m_removeAnniversary, definitionMask, &error)
            && writeDetails<QContactAvatar>(contactId, contact, m_removeAvatar, definitionMask, &error)
            && writeDetails<QContactBirthday>(contactId, contact, m_removeBirthday, definitionMask, &error)
            && writeDetails<QContactEmailAddress>(contactId, contact, m_removeEmailAddress, definitionMask, &error)
            && writeDetails<QContactGlobalPresence>(contactId, contact, m_removeGlobalPresence, definitionMask, &error)
            && writeDetails<QContactGuid>(contactId, contact, m_removeGuid, definitionMask, &error)
            && writeDetails<QContactHobby>(contactId, contact, m_removeHobby, definitionMask, &error)
            && writeDetails<QContactNickname>(contactId, contact, m_removeNickname, definitionMask, &error)
            && writeDetails<QContactNote>(contactId, contact, m_removeNote, definitionMask, &error)
            && writeDetails<QContactOnlineAccount>(contactId, contact, m_removeOnlineAccount, definitionMask, &error)
            && writeDetails<QContactOrganization>(contactId, contact, m_removeOrganization, definitionMask, &error)
            && writeDetails<QContactPhoneNumber>(contactId, contact, m_removePhoneNumber, definitionMask, &error)
            && writeDetails<QContactPresence>(contactId, contact, m_removePresence, definitionMask, &error)
            && writeDetails<QContactRingtone>(contactId, contact, m_removeRingtone, definitionMask, &error)
            && writeDetails<QContactTag>(contactId, contact, m_removeTag, definitionMask, &error)
            && writeDetails<QContactUrl>(contactId, contact, m_removeUrl, definitionMask, &error)
            && writeDetails<QContactOriginMetadata>(contactId, contact, m_removeOriginMetadata, definitionMask, &error)) {
        return QContactManager::NoError;
    }
    return error;
}

void ContactWriter::bindContactDetails(const QContact &contact, QSqlQuery &query, const DetailList &definitionMask, bool update)
{
#ifdef USING_QTPIM
    QContactDisplayLabel label = contact.detail<QContactDisplayLabel>();
    if (!label.isEmpty()) {
        query.bindValue(0, label.label().trimmed());
    }
#else
    query.bindValue(0, contact.displayLabel().trimmed());
#endif

    const QContactName name = contact.detail<QContactName>();
    const QString firstName(name.value<QString>(QContactName::FieldFirstName).trimmed());
    const QString lastName(name.value<QString>(QContactName::FieldLastName).trimmed());

    query.bindValue(1, firstName);
    query.bindValue(2, firstName.toLower());
    query.bindValue(3, lastName);
    query.bindValue(4, lastName.toLower());
    query.bindValue(5, name.value<QString>(QContactName::FieldMiddleName).trimmed());
    query.bindValue(6, name.value<QString>(QContactName::FieldPrefix).trimmed());
    query.bindValue(7, name.value<QString>(QContactName::FieldSuffix).trimmed());
#ifdef USING_QTPIM
    query.bindValue(8, name.value<QString>(QContactName__FieldCustomLabel).trimmed());
#else
    query.bindValue(8, name.value<QString>(QContactName::FieldCustomLabel).trimmed());
#endif

    const QContactSyncTarget starget = contact.detail<QContactSyncTarget>();
    QString stv = starget.syncTarget();
    if (stv.isEmpty())
        stv = QLatin1String("local"); // by default, it is a "local device" contact.
    query.bindValue(9, stv);

    const QContactTimestamp timestamp = contact.detail<QContactTimestamp>();
    query.bindValue(10, timestamp.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp).toUTC());
    query.bindValue(11, timestamp.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp).toUTC());

    const QContactGender gender = contact.detail<QContactGender>();
#ifdef USING_QTPIM
    const QString gv(gender.gender() == QContactGender::GenderFemale ? QString::fromLatin1("Female") :
                     gender.gender() == QContactGender::GenderMale ? QString::fromLatin1("Male") : QString());
#else
    const QString gv(gender.value<QString>(QContactGender::FieldGender).trimmed());
#endif
    query.bindValue(12, gv);

    const QContactFavorite favorite = contact.detail<QContactFavorite>();
    query.bindValue(13, favorite.isFavorite());

    // Does this contact contain the information needed to update hasPhoneNumber?
    bool valueKnown = definitionMask.isEmpty() || detailListContains<QContactPhoneNumber>(definitionMask);
    bool value = valueKnown ? !contact.detail<QContactPhoneNumber>().isEmpty() : false;
    if (update) {
        query.bindValue(14, valueKnown);
        query.bindValue(15, value);
    } else {
        query.bindValue(14, value);
    }

    valueKnown = definitionMask.isEmpty() || detailListContains<QContactEmailAddress>(definitionMask);
    value = valueKnown ? !contact.detail<QContactEmailAddress>().isEmpty() : false;
    if (update) {
        query.bindValue(16, valueKnown);
        query.bindValue(17, value);
    } else {
        query.bindValue(15, value);
    }

    valueKnown = definitionMask.isEmpty() || detailListContains<QContactOnlineAccount>(definitionMask);
    value = valueKnown ? !contact.detail<QContactOnlineAccount>().isEmpty() : false;
    if (update) {
        query.bindValue(18, valueKnown);
        query.bindValue(19, value);
    } else {
        query.bindValue(16, value);
    }

    // isOnline is true if any presence details are not offline/unknown
    valueKnown = definitionMask.isEmpty() || detailListContains<QContactPresence>(definitionMask);
    value = false;
    foreach (const QContactPresence &presence, contact.details<QContactPresence>()) {
        if (presence.presenceState() >= QContactPresence::PresenceAvailable &&
            presence.presenceState() <= QContactPresence::PresenceExtendedAway) {
            value = true;
            break;
        }
    }
    if (update) {
        query.bindValue(20, valueKnown);
        query.bindValue(21, value);
    } else {
        query.bindValue(17, value);
    }
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactAddress &detail)
{
    typedef QContactAddress T;
    m_insertAddress.bindValue(0, contactId);
    m_insertAddress.bindValue(1, detail.value<QString>(T::FieldStreet).trimmed());
    m_insertAddress.bindValue(2, detail.value<QString>(T::FieldPostOfficeBox).trimmed());
    m_insertAddress.bindValue(3, detail.value<QString>(T::FieldRegion).trimmed());
    m_insertAddress.bindValue(4, detail.value<QString>(T::FieldLocality).trimmed());
    m_insertAddress.bindValue(5, detail.value<QString>(T::FieldPostcode).trimmed());
    m_insertAddress.bindValue(6, detail.value<QString>(T::FieldCountry).trimmed());
    return m_insertAddress;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactAnniversary &detail)
{
    typedef QContactAnniversary T;
    m_insertAnniversary.bindValue(0, contactId);
    m_insertAnniversary.bindValue(1, detailValue(detail, T::FieldOriginalDate));
    m_insertAnniversary.bindValue(2, detailValue(detail, T::FieldCalendarId));
#ifdef USING_QTPIM
    m_insertAnniversary.bindValue(3, Anniversary::subType(detail.subType()));
#else
    m_insertAnniversary.bindValue(3, detailValue(detail, T::FieldSubType));
#endif
    return m_insertAnniversary;
}


QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactAvatar &detail)
{
    typedef QContactAvatar T;
    m_insertAvatar.bindValue(0, contactId);
    m_insertAvatar.bindValue(1, detail.value<QString>(T::FieldImageUrl).trimmed());
    m_insertAvatar.bindValue(2, detail.value<QString>(T::FieldVideoUrl).trimmed());
    m_insertAvatar.bindValue(3, detailValue(detail, QContactAvatar__FieldAvatarMetadata));
    return m_insertAvatar;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactBirthday &detail)
{
    typedef QContactBirthday T;
    m_insertBirthday.bindValue(0, contactId);
    m_insertBirthday.bindValue(1, detailValue(detail, T::FieldBirthday));
    m_insertBirthday.bindValue(2, detailValue(detail, T::FieldCalendarId));
    return m_insertBirthday;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactEmailAddress &detail)
{
    typedef QContactEmailAddress T;
    const QString address(detail.value<QString>(T::FieldEmailAddress).trimmed());
    m_insertEmailAddress.bindValue(0, contactId);
    m_insertEmailAddress.bindValue(1, address);
    m_insertEmailAddress.bindValue(2, address.toLower());
    return m_insertEmailAddress;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactGlobalPresence &detail)
{
    typedef QContactGlobalPresence T;
    m_insertGlobalPresence.bindValue(0, contactId);
    m_insertGlobalPresence.bindValue(1, detailValue(detail, T::FieldPresenceState));
    m_insertGlobalPresence.bindValue(2, detailValue(detail, T::FieldTimestamp));
    m_insertGlobalPresence.bindValue(3, detail.value<QString>(T::FieldNickname).trimmed());
    m_insertGlobalPresence.bindValue(4, detail.value<QString>(T::FieldCustomMessage).trimmed());
    return m_insertGlobalPresence;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactGuid &detail)
{
    typedef QContactGuid T;
    m_insertGuid.bindValue(0, contactId);
    m_insertGuid.bindValue(1, detailValue(detail, T::FieldGuid));
    return m_insertGuid;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactHobby &detail)
{
    typedef QContactHobby T;
    m_insertHobby.bindValue(0, contactId);
    m_insertHobby.bindValue(1, detailValue(detail, T::FieldHobby));
    return m_insertHobby;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactNickname &detail)
{
    typedef QContactNickname T;
    const QString nickname(detail.value<QString>(T::FieldNickname).trimmed());
    m_insertNickname.bindValue(0, contactId);
    m_insertNickname.bindValue(1, nickname);
    m_insertNickname.bindValue(2, nickname.toLower());
    return m_insertNickname;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactNote &detail)
{
    typedef QContactNote T;
    m_insertNote.bindValue(0, contactId);
    m_insertNote.bindValue(1, detailValue(detail, T::FieldNote));
    return m_insertNote;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactOnlineAccount &detail)
{
    typedef QContactOnlineAccount T;
    const QString uri(detail.value<QString>(T::FieldAccountUri).trimmed());
    m_insertOnlineAccount.bindValue(0, contactId);
    m_insertOnlineAccount.bindValue(1, uri);
    m_insertOnlineAccount.bindValue(2, uri.toLower());
#ifdef USING_QTPIM
    m_insertOnlineAccount.bindValue(3, OnlineAccount::protocol(detail.protocol()));
#else
    m_insertOnlineAccount.bindValue(3, detailValue(detail, T::FieldProtocol));
#endif
    m_insertOnlineAccount.bindValue(4, detailValue(detail, T::FieldServiceProvider));
    m_insertOnlineAccount.bindValue(5, detailValue(detail, T::FieldCapabilities));
#ifdef USING_QTPIM
    m_insertOnlineAccount.bindValue(6, OnlineAccount::subTypeList(detail.subTypes()).join(QLatin1String(";")));
#else
    m_insertOnlineAccount.bindValue(6, detailValue(detail, T::FieldSubTypes));
#endif
    m_insertOnlineAccount.bindValue(7, detailValue(detail, QContactOnlineAccount__FieldAccountPath));
    m_insertOnlineAccount.bindValue(8, detailValue(detail, QContactOnlineAccount__FieldAccountIconPath));
    m_insertOnlineAccount.bindValue(9, detailValue(detail, QContactOnlineAccount__FieldEnabled));
    return m_insertOnlineAccount;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactOrganization &detail)
{
    typedef QContactOrganization T;
    m_insertOrganization.bindValue(0, contactId);
    m_insertOrganization.bindValue(1, detail.value<QString>(T::FieldName).trimmed());
    m_insertOrganization.bindValue(2, detail.value<QString>(T::FieldRole).trimmed());
    m_insertOrganization.bindValue(3, detail.value<QString>(T::FieldTitle).trimmed());
    m_insertOrganization.bindValue(4, detail.value<QString>(T::FieldLocation).trimmed());
    m_insertOrganization.bindValue(5, detail.department().join(QLatin1String(";")));
    m_insertOrganization.bindValue(6, detail.value<QString>(T::FieldLogoUrl).trimmed());
    return m_insertOrganization;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactPhoneNumber &detail)
{
    typedef QContactPhoneNumber T;
    m_insertPhoneNumber.bindValue(0, contactId);
    m_insertPhoneNumber.bindValue(1, detail.value<QString>(T::FieldNumber).trimmed());
#ifdef USING_QTPIM
    m_insertPhoneNumber.bindValue(2, PhoneNumber::subTypeList(detail.subTypes()).join(QLatin1String(";")));
#else
    m_insertPhoneNumber.bindValue(2, detailValue(detail, T::FieldSubTypes));
#endif
    m_insertPhoneNumber.bindValue(3, QVariant(ContactsEngine::normalizedPhoneNumber(detail.number())));
    return m_insertPhoneNumber;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactPresence &detail)
{
    typedef QContactPresence T;
    m_insertPresence.bindValue(0, contactId);
    m_insertPresence.bindValue(1, detailValue(detail, T::FieldPresenceState));
    m_insertPresence.bindValue(2, detailValue(detail, T::FieldTimestamp));
    m_insertPresence.bindValue(3, detail.value<QString>(T::FieldNickname).trimmed());
    m_insertPresence.bindValue(4, detail.value<QString>(T::FieldCustomMessage).trimmed());
    return m_insertPresence;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactRingtone &detail)
{
    typedef QContactRingtone T;
    m_insertRingtone.bindValue(0, contactId);
    m_insertRingtone.bindValue(1, detail.value<QString>(T::FieldAudioRingtoneUrl).trimmed());
    m_insertRingtone.bindValue(2, detail.value<QString>(T::FieldVideoRingtoneUrl).trimmed());
    return m_insertRingtone;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactTag &detail)
{
    typedef QContactTag T;
    m_insertTag.bindValue(0, contactId);
    m_insertTag.bindValue(1, detail.value<QString>(T::FieldTag).trimmed());
    return m_insertTag;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactUrl &detail)
{
    typedef QContactUrl T;
    m_insertUrl.bindValue(0, contactId);
    m_insertUrl.bindValue(1, detail.value<QString>(T::FieldUrl).trimmed());
#ifdef USING_QTPIM
    m_insertUrl.bindValue(2, Url::subType(detail.subType()));
#else
    m_insertUrl.bindValue(2, detailValue(detail, T::FieldSubType));
#endif
    return m_insertUrl;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactOriginMetadata &detail)
{
    m_insertOriginMetadata.bindValue(0, contactId);
    m_insertOriginMetadata.bindValue(1, detailValue(detail, QContactOriginMetadata::FieldId));
    m_insertOriginMetadata.bindValue(2, detailValue(detail, QContactOriginMetadata::FieldGroupId));
    m_insertOriginMetadata.bindValue(3, detailValue(detail, QContactOriginMetadata::FieldEnabled));
    return m_insertOriginMetadata;
}
