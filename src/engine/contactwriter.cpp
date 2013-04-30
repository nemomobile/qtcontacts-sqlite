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

#include "contactreader.h"
#include "contactnotifier.h"

#include <QContactFavorite>
#include <QContactGender>
#include <QContactGlobalPresence>
#include <QContactName>
#include <QContactSyncTarget>
#include <QContactTimestamp>

#include <QSqlError>
#include <QVector>

#include <QtDebug>

static const char *findRelatedForAggregate =
        "\n SELECT contactId FROM Contacts WHERE contactId IN ("
        "\n SELECT secondId FROM Relationships WHERE firstId = :aggregateId AND type = 'Aggregates')";

static const char *findLocalForAggregate =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'local' AND contactId IN ("
        "\n SELECT secondId FROM Relationships WHERE firstId = :aggregateId AND type = 'Aggregates')";

static const char *findAggregateForLocal =
        "\n SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates' AND secondId = :localId";

static const char *selectAggregateContactIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'aggregate' AND contactId = :possibleAggregateId";

static const char *orphanAggregateIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'aggregate' AND contactId NOT IN ("
        "\n SELECT firstId FROM Relationships WHERE type = 'Aggregates')";

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
        "\n  lastName,"
        "\n  middleName,"
        "\n  prefix,"
        "\n  suffix,"
        "\n  customLabel,"
        "\n  syncTarget,"
        "\n  created,"
        "\n  modified,"
        "\n  gender,"
        "\n  isFavorite)"
        "\n VALUES ("
        "\n  :displayLabel,"
        "\n  :firstName,"
        "\n  :lastName,"
        "\n  :middleName,"
        "\n  :prefix,"
        "\n  :suffix,"
        "\n  :customLabel,"
        "\n  :syncTarget,"
        "\n  :created,"
        "\n  :modified,"
        "\n  :gender,"
        "\n  :isFavorite);";

static const char *updateContact =
        "\n UPDATE Contacts SET"
        "\n  displayLabel = :displayLabel,"
        "\n  firstName = :firstName,"
        "\n  lastName = :lastName,"
        "\n  middleName = :middleName,"
        "\n  prefix = :prefix,"
        "\n  suffix = :suffix,"
        "\n  customLabel = :customLabel,"
        "\n  syncTarget = :syncTarget,"
        "\n  created = :created,"
        "\n  modified = :modified,"
        "\n  gender = :gender,"
        "\n  isFavorite = :isFavorite"
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
        "\n  emailAddress)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :emailAddress)";

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
        "\n  nickname)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :nickname)";

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

static const char *insertTpMetadata =
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
        "\n  contexts)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :detailId,"
        "\n  :detail,"
        "\n  :detailUri,"
        "\n  :linkedDetailUris,"
        "\n  :contexts);";

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

ContactWriter::ContactWriter(const QSqlDatabase &database, ContactReader *reader)
    : m_database(database)
    , m_findRelatedForAggregate(prepare(findRelatedForAggregate, database))
    , m_findLocalForAggregate(prepare(findLocalForAggregate, database))
    , m_findAggregateForLocal(prepare(findAggregateForLocal, database))
    , m_selectAggregateContactIds(prepare(selectAggregateContactIds, database))
    , m_orphanAggregateIds(prepare(orphanAggregateIds, database))
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
    , m_insertTpMetadata(prepare(insertTpMetadata, database))
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
    , m_removeTpMetadata(prepare("DELETE FROM TpMetadata WHERE contactId = :contactId;", database))
    , m_removeDetail(prepare("DELETE FROM Details WHERE contactId = :contactId AND detail = :detail;", database))
    , m_removeIdentity(prepare("DELETE FROM Identities WHERE identity = :identity;", database))
    , m_reader(reader)
{
}

ContactWriter::~ContactWriter()
{
}

QContactManager::Error ContactWriter::setIdentity(
        ContactsDatabase::Identity identity, QContactLocalId contactId)
{
    QSqlQuery *query = 0;
    if (contactId != 0) {
        m_insertIdentity.bindValue(0, identity);
        m_insertIdentity.bindValue(1, contactId - 1);
        query = &m_insertIdentity;
    } else {
        m_removeIdentity.bindValue(0, identity);
        query = &m_removeIdentity;
    }

    if (query->exec()) {
        // Notify..

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
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap)
{
    if (relationships.isEmpty())
        return QContactManager::NoError;

    // in order to perform duplicate detection we build up the following datastructure.
    QMultiMap<QContactLocalId, QPair<QString, QContactLocalId> > bucketedRelationships; // first id to <type, second id>.
    {
        if (!m_existingRelationships.exec()) {
            qWarning() << "Failed to fetch existing relationships for duplicate detection during insert:";
            qWarning() << m_existingRelationships.lastError();
            return QContactManager::UnspecifiedError;
        }

        while (m_existingRelationships.next()) {
            QContactLocalId fid = (m_existingRelationships.value(0).toUInt() + 1);
            QContactLocalId sid = (m_existingRelationships.value(1).toUInt() + 1);
            QString rt = m_existingRelationships.value(2).toString();
            bucketedRelationships.insert(fid, QPair<QString, QContactLocalId>(rt, sid));
        }
    }

    // in order to perform validity detection we build up the following set.
    // XXX TODO: use foreign key constraint or similar in Relationships table?
    QSet<QContactLocalId> validContactIds;
    {
        if (!m_existingContactIds.exec()) {
            qWarning() << "Failed to fetch existing contacts for validity detection during insert:";
            qWarning() << m_existingContactIds.lastError();
            return QContactManager::UnspecifiedError;
        }

        while (m_existingContactIds.next()) {
            validContactIds.insert(m_existingContactIds.value(0).toUInt() + 1);
        }
    }

    QList<QContactLocalId> firstIdsToBind;
    QList<QContactLocalId> secondIdsToBind;
    QList<QString> typesToBind;

    QSqlQuery multiInsertQuery(m_database);
    QString queryString = QLatin1String("INSERT INTO Relationships");
    int realInsertions = 0;
    int invalidInsertions = 0;
    for (int i = 0; i < relationships.size(); ++i) {
        const QContactRelationship &relationship = relationships.at(i);
        const QContactLocalId firstId = relationship.first().localId();
        const QContactLocalId secondId = relationship.second().localId();
        const QString &type = relationship.relationshipType();

        if ((firstId == secondId)
                || (!relationship.first().managerUri().isEmpty()
                    && relationship.first().managerUri() != QLatin1String("org.nemomobile.contacts.sqlite"))
                || (!relationship.second().managerUri().isEmpty()
                    && relationship.second().managerUri() != QLatin1String("org.nemomobile.contacts.sqlite"))
                || (!validContactIds.contains(firstId) || !validContactIds.contains(secondId))) {
            // invalid contact specified in relationship, don't insert.
            invalidInsertions += 1;
            if (errorMap)
                errorMap->insert(i, QContactManager::InvalidRelationshipError);
            continue;
        }

        if (bucketedRelationships.find(firstId, QPair<QString, QContactLocalId>(type, secondId)) != bucketedRelationships.end()) {
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
            bucketedRelationships.insert(firstId, QPair<QString, QContactLocalId>(type, secondId));
            realInsertions += 1;
        }
    }

    if (realInsertions > 0 && !multiInsertQuery.prepare(queryString)) {
        qWarning() << "Failed to prepare multiple insert relationships query";
        qWarning() << multiInsertQuery.lastError();
        qWarning() << "Query:\n" << queryString;
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < realInsertions; ++i) {
        multiInsertQuery.bindValue(QString(QLatin1String(":firstId%1")).arg(QString::number(i)), (firstIdsToBind.at(i) - 1));
        multiInsertQuery.bindValue(QString(QLatin1String(":secondId%1")).arg(QString::number(i)), (secondIdsToBind.at(i) - 1));
        multiInsertQuery.bindValue(QString(QLatin1String(":type%1")).arg(QString::number(i)), typesToBind.at(i));
    }

    if (realInsertions > 0 && !multiInsertQuery.exec()) {
        qWarning() << "Failed to insert relationships";
        qWarning() << multiInsertQuery.lastError();
        qWarning() << "Query:\n" << queryString;
        return QContactManager::UnspecifiedError;
    }

    // Notify

    if (invalidInsertions > 0) {
        return QContactManager::InvalidRelationshipError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap)
{
    if (relationships.isEmpty())
        return QContactManager::NoError;

    // in order to perform existence detection we build up the following datastructure.
    QMultiMap<QContactLocalId, QPair<QString, QContactLocalId> > bucketedRelationships; // first id to <type, second id>.
    {
        if (!m_existingRelationships.exec()) {
            qWarning() << "Failed to fetch existing relationships for duplicate detection during insert:";
            qWarning() << m_existingRelationships.lastError();
            return QContactManager::UnspecifiedError;
        }

        while (m_existingRelationships.next()) {
            QContactLocalId fid = (m_existingRelationships.value(0).toUInt() + 1);
            QContactLocalId sid = (m_existingRelationships.value(1).toUInt() + 1);
            QString rt = m_existingRelationships.value(2).toString();
            bucketedRelationships.insert(fid, QPair<QString, QContactLocalId>(rt, sid));
        }
    }

    QContactManager::Error worstError = QContactManager::NoError;
    QSet<QContactRelationship> alreadyRemoved;
    bool removeInvalid = false;
    for (int i = 0; i < relationships.size(); ++i) {
        QContactRelationship curr = relationships.at(i);
        if (alreadyRemoved.contains(curr)) {
            continue;
        }

        if (bucketedRelationships.find(curr.first().localId(), QPair<QString, QContactLocalId>(curr.relationshipType(), curr.second().localId())) == bucketedRelationships.end()) {
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

        removeRelationship.bindValue(":firstId", curr.first().localId() - 1);
        removeRelationship.bindValue(":secondId", curr.second().localId() - 1);
        removeRelationship.bindValue(":type", curr.relationshipType());

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

    // Notify

    if (removeInvalid) {
        return QContactManager::DoesNotExistError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(const QList<QContactLocalId> &contactIds, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    if (contactIds.isEmpty())
        return QContactManager::NoError;

    // grab the self-contact id so we can avoid removing it.
    QContactLocalId selfContactId = 0;
    m_selfContactId.bindValue(":identity", ContactsDatabase::SelfContactId);
    if (!m_selfContactId.exec()) {
        qWarning() << "Failed to fetch self contact id during remove";
        qWarning() << m_selfContactId.lastError();
        return QContactManager::UnspecifiedError;
    }
    if (m_selfContactId.next()) {
        selfContactId = m_selfContactId.value(0).toUInt() + 1;
    }

    // grab the existing contact ids so that we can perform removal detection
    // XXX TODO: for perf, remove this check.  Less conformant, but client
    // shouldn't care (ie, not exists == has been removed).
    QSet<QContactLocalId> existingContactIds;
    if (!m_existingContactIds.exec()) {
        qWarning() << "Failed to fetch existing contact ids during remove";
        qWarning() << m_existingContactIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_existingContactIds.next()) {
        existingContactIds.insert(m_existingContactIds.value(0).toUInt() + 1);
    }

    // determine which contacts we actually need to remove
    QContactManager::Error error = QContactManager::NoError;
    QList<QContactLocalId> realRemoveIds;
    QVariantList boundRealRemoveIds;
    for (int i = 0; i < contactIds.size(); ++i) {
        QContactLocalId currLId = contactIds.at(i);
        if (selfContactId > 0 && currLId == selfContactId) {
            if (errorMap)
                errorMap->insert(i, QContactManager::BadArgumentError);
            error = QContactManager::BadArgumentError;
        } else if (existingContactIds.contains(currLId)) {
            realRemoveIds.append(currLId);
            boundRealRemoveIds.append(currLId - 1);
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
        if (!withinTransaction && !m_database.transaction()) {
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
                m_database.rollback();
            }
            return QContactManager::UnspecifiedError;
        }
        if (!withinTransaction && !m_database.commit()) {
            // only commit if we created a transaction.
            qWarning() << "Failed to commit removal";
            m_database.rollback();
            return QContactManager::UnspecifiedError;
        }
        ContactNotifier::contactsRemoved(realRemoveIds);
    }
    return error;
#else
    // grab the ids of aggregate contacts which aggregate any of the contacts
    // which we're about to remove.  We will regenerate them after successful
    // remove.  Also grab the ids of aggregates which are being removed, so
    // we can remove all related contacts.
    QList<QContactLocalId> aggregatesOfRemoved;
    QList<QContactLocalId> aggregatesToRemove;
    m_selectAggregateContactIds.bindValue(":possibleAggregateId", boundRealRemoveIds);
    if (!m_selectAggregateContactIds.execBatch()) {
        qWarning() << "Failed to select aggregate contact ids during remove";
        qWarning() << m_selectAggregateContactIds.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_selectAggregateContactIds.next()) {
        aggregatesToRemove.append(m_selectAggregateContactIds.value(0).toUInt() + 1);
    }
    m_findAggregateForLocal.bindValue(":localId", boundRealRemoveIds);
    if (!m_findAggregateForLocal.execBatch()) {
        qWarning() << "Failed to fetch aggregator contact ids during remove";
        qWarning() << m_findAggregateForLocal.lastError();
        return QContactManager::UnspecifiedError;
    }
    while (m_findAggregateForLocal.next()) {
        aggregatesOfRemoved.append(m_findAggregateForLocal.value(0).toUInt() + 1);
    }

    QVariantList boundNonAggregatesToRemove;
    QVariantList boundAggregatesToRemove;
    foreach (QContactLocalId rrid, realRemoveIds) {
        if (!aggregatesToRemove.contains(rrid)) {
            // this is a non-aggregate contact which we need to remove
            boundNonAggregatesToRemove.append(rrid - 1);
        } else {
            // this is an aggregate contact which we need to remove
            boundAggregatesToRemove.append(rrid - 1);
        }
    }

    if (!withinTransaction && !m_database.transaction()) {
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
                m_database.rollback();
            }
            return QContactManager::UnspecifiedError;
        }
    }

    // remove the aggregate contacts - and any contacts they aggregate
    if (boundAggregatesToRemove.size() > 0) {
        // first, get the list of contacts which are aggregated by the aggregate contacts we are going to remove
        m_findRelatedForAggregate.bindValue(":aggregateId", boundAggregatesToRemove);
        if (!m_findRelatedForAggregate.execBatch()) {
            qWarning() << "Failed to fetch contacts aggregated by removed aggregates";
            qWarning() << m_findRelatedForAggregate.lastError();
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                m_database.rollback();
            }
            return QContactManager::UnspecifiedError;
        }
        while (m_findRelatedForAggregate.next()) {
            quint32 dbId = m_findRelatedForAggregate.value(0).toUInt();
            QContactLocalId currToRemove = dbId + 1;
            boundAggregatesToRemove.append(dbId); // we just add it to the big list of bound "remove these"
            realRemoveIds.append(currToRemove);
        }

        // remove the aggregates + the aggregated
        m_removeContact.bindValue(QLatin1String(":contactId"), boundAggregatesToRemove);
        if (!m_removeContact.execBatch()) {
            qWarning() << "Failed to removed aggregate contacts (and the contacts they aggregate)";
            qWarning() << m_removeContact.lastError();
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                m_database.rollback();
            }
            return QContactManager::UnspecifiedError;
        }
    }

    // removing aggregates if they no longer aggregate any contacts.
    QVariantList boundOrphans;
    if (!m_orphanAggregateIds.exec()) {
        qWarning() << "Failed to fetch orphan aggregate contact ids during remove";
        qWarning() << m_orphanAggregateIds.lastError();
        if (!withinTransaction) {
            // only rollback the transaction if we created it
            m_database.rollback();
        }
        return QContactManager::UnspecifiedError;
    }
    while (m_orphanAggregateIds.next()) {
        quint32 orphan = m_orphanAggregateIds.value(0).toUInt();
        boundOrphans.append(orphan);
        realRemoveIds.append(orphan + 1);
    }

    if (boundOrphans.size() > 0) {
        m_removeContact.bindValue(QLatin1String(":contactId"), boundOrphans);
        if (!m_removeContact.execBatch()) {
            qWarning() << "Failed to remove orphaned aggregate contacts";
            qWarning() << m_removeContact.lastError();
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                m_database.rollback();
            }
            return QContactManager::UnspecifiedError;
        }
    }

    // Success!  If we created a transaction, commit.
    if (!withinTransaction && !m_database.commit()) {
        qWarning() << "Failed to commit database after removal";
        m_database.rollback();
        return QContactManager::UnspecifiedError;
    }

    // And notify of any removals.
    if (realRemoveIds.size() > 0) {
        // update our "regenerate list" by purging removed contacts
        foreach (QContactLocalId removedId, realRemoveIds) {
            aggregatesOfRemoved.removeAll(removedId);
        }

        // emit removed signals
        ContactNotifier::contactsRemoved(realRemoveIds);
    }

    // Now regenerate our remaining aggregates as required.
    // note that we commit() the database prior to this point,
    // as the write process could cause temporary tables to be
    // created, and if it fails it's not catastrophic (just
    // means that the aggregate retains some stale data).
    if (aggregatesOfRemoved.size() > 0) {
        regenerateAggregates(aggregatesOfRemoved, QStringList(), withinTransaction);
    }

    return error;
#endif
}

template <typename T> bool ContactWriter::removeCommonDetails(
            QContactLocalId contactId, QContactManager::Error *error)
{
    m_removeDetail.bindValue(0, contactId);
    m_removeDetail.bindValue(1, T::DefinitionName);

    if (!m_removeDetail.exec()) {
        qWarning() << "Failed to remove common detail for" << QLatin1String(T::DefinitionName);
        qWarning() << m_removeDetail.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }
    return true;
}

template <typename T> bool ContactWriter::writeCommonDetails(
            QContactLocalId contactId, const QSqlQuery &query, const T &detail, QContactManager::Error *error)
{
    const QVariant detailUri = detail.variantValue(QContactDetail::FieldDetailUri);
    const QVariant linkedDetailUris = detail.variantValue(QContactDetail::FieldLinkedDetailUris);
    const QVariant contexts = detail.variantValue(QContactDetail::FieldContext);

    if (detailUri.isValid() || linkedDetailUris.isValid() || contexts.isValid()) {
        m_insertDetail.bindValue(0, contactId);
        m_insertDetail.bindValue(1, query.lastInsertId());
        m_insertDetail.bindValue(2, T::DefinitionName);
        m_insertDetail.bindValue(3, detailUri);
        m_insertDetail.bindValue(4, linkedDetailUris.toStringList().join(QLatin1String(";")));
        m_insertDetail.bindValue(5, contexts.toStringList().join(QLatin1String(";")));

        if (!m_insertDetail.exec()) {
            qWarning() << "Failed to write common details for" << QLatin1String(T::DefinitionName);
            qWarning() << m_insertDetail.lastError();
            *error = QContactManager::UnspecifiedError;
            return false;
        }
    }
    return true;
}

template <typename T> bool ContactWriter::writeDetails(
        QContactLocalId contactId,
        QContact *contact,
        QSqlQuery &removeQuery,
        const QStringList &definitionMask,
        QContactManager::Error *error)
{
    if (!definitionMask.isEmpty() && !definitionMask.contains(T::DefinitionName))
        return true;

    if (!removeCommonDetails<T>(contactId, error))
        return false;

    removeQuery.bindValue(0, contactId);
    if (!removeQuery.exec()) {
        qWarning() << "Failed to remove existing details for" << QLatin1String(T::DefinitionName);
        qWarning() << removeQuery.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    foreach (const T &detail, contact->details<T>()) {
        QSqlQuery &query = bindDetail(contactId, detail);
        if (!query.exec()) {
            qWarning() << "Failed to write details for" << QLatin1String(T::DefinitionName);
            qWarning() << query.lastError();
            *error = QContactManager::UnspecifiedError;
            return false;
        }

        if (!writeCommonDetails(contactId, query, detail, error))
            return false;
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

template <> bool ContactWriter::writeDetails<QContactPresence>(
        QContactLocalId contactId,
        QContact *contact,
        QSqlQuery &removeQuery,
        const QStringList &definitionMask,
        QContactManager::Error *error)
{
    if (!definitionMask.isEmpty() && !definitionMask.contains(QContactPresence::DefinitionName))
        return true;

    if (!removeCommonDetails<QContactPresence>(contactId, error))
        return false;

    removeQuery.bindValue(0, contactId);
    if (!removeQuery.exec()) {
        qWarning() << "Failed to remove existing details for" << QLatin1String(QContactPresence::DefinitionName);
        qWarning() << removeQuery.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    m_removeGlobalPresence.bindValue(0, contactId);
    if (!m_removeGlobalPresence.exec()) {
        qWarning() << "Failed to remove existing details for" << QLatin1String(QContactGlobalPresence::DefinitionName);
        qWarning() << removeQuery.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }

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

        QSqlQuery &query = bindDetail(contactId, detail);
        if (!query.exec()) {
            qWarning() << "Failed to write details for" << QLatin1String(QContactPresence::DefinitionName);
            qWarning() << query.lastError();
            *error = QContactManager::UnspecifiedError;
            return false;
        }

        if (!writeCommonDetails(contactId, query, detail, error))
            return false;
    }

    m_insertGlobalPresence.bindValue(0, contactId);
    m_insertGlobalPresence.bindValue(1, bestPresence.presenceState());
    m_insertGlobalPresence.bindValue(2, bestPresence.timestamp());
    m_insertGlobalPresence.bindValue(3, bestPresence.nickname());
    m_insertGlobalPresence.bindValue(4, bestPresence.customMessage());
    if (!m_insertGlobalPresence.exec()) {
        qWarning() << "Failed to write details for" << QLatin1String(QContactGlobalPresence::DefinitionName);
        qWarning() << m_insertGlobalPresence.lastError();
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    globalPresence.setPresenceState(bestPresence.presenceState());
    globalPresence.setTimestamp(bestPresence.timestamp());
    globalPresence.setNickname(bestPresence.nickname());
    globalPresence.setCustomMessage(bestPresence.customMessage());

    contact->saveDetail(&globalPresence);

    return true;
}

QContactManager::Error ContactWriter::save(
            QList<QContact> *contacts,
            const QStringList &definitionMask,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate)
{
    if (contacts->isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !m_database.transaction()) {
        // only create a transaction if we're not within one already
        qWarning() << "Unable to begin database transaction while saving contacts";
        return QContactManager::UnspecifiedError;
    }

    QVector<QContactLocalId> createdContactIds;
    QVector<QContactLocalId> updatedContactIds;

    QContactManager::Error worstError = QContactManager::NoError;
    QContactManager::Error err = QContactManager::NoError;
    for (int i = 0; i < contacts->count(); ++i) {
        QContact &contact = (*contacts)[i];
        const QContactLocalId contactId = contact.localId();
        if (contactId == 0) {
            err = create(&contact, definitionMask, true);
            createdContactIds.append(contact.localId());
        } else {
            err = update(&contact, definitionMask, true, withinAggregateUpdate);
            updatedContactIds.append(contactId);
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
        if (err != QContactManager::NoError) {
            m_database.rollback();
            return worstError;
        } else if (!m_database.commit()) {
            qWarning() << "Failed to commit contacts";
            qWarning() << m_database.lastError();
            m_database.rollback();
            return QContactManager::UnspecifiedError;
        }
    }

    if (!createdContactIds.isEmpty())
        ContactNotifier::contactsAdded(createdContactIds);
    if (!updatedContactIds.isEmpty())
        ContactNotifier::contactsChanged(updatedContactIds);

    return worstError;
}

/*
    This function is called as part of the "save updated aggregate"
    codepath.  It calculates the list of details which were modified
    in the updated version (compared to the current database version)
    and returns it.
*/
static QContactManager::Error calculateDelta(ContactReader *reader, QContact *contact, const QStringList &definitionMask, QList<QContactDetail> *delta)
{
    QMap<int, QContactManager::Error> errorMap;
    QList<QContactLocalId> whichList;
    whichList.append(contact->id().localId());
    QList<QContact> readList;
    QContactManager::Error readError = reader->readContacts(QLatin1String("UpdateAggregate"), &readList, whichList, definitionMask);
    if (readError != QContactManager::NoError || readList.size() == 0) {
        // unable to read the aggregate contact from the database
        return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
    }

    // Determine which details are in the update contact which aren't in the database contact:
    QContact dbContact(readList.at(0)); // database version
    QContact upContact(*contact);       // update version
    QList<QContactDetail> dbDetails = dbContact.details();
    QList<QContactDetail> upDetails = upContact.details();

    // Detail order is not defined, so loop over the entire set for each, removing exact matches
    foreach(QContactDetail ddb, dbDetails) {
        foreach(QContactDetail dup, upDetails) {
            if(ddb == dup) {
                dbContact.removeDetail(&ddb);
                upContact.removeDetail(&dup);
                break;
            }
        }
    }

    // Also remove any superset details (eg, backend added a field (like lastModified to timestamp) on previous save)
    dbDetails = dbContact.details();
    upDetails = upContact.details();
    foreach (QContactDetail ddb, dbDetails) {
        foreach (QContactDetail dup, upDetails) {
            if (ddb.definitionName() == dup.definitionName()) {
                bool dbIsSuperset = true;
                QMap<QString, QVariant> dupmap = dup.variantValues();
                foreach (QString key, dupmap.keys()) {
                    if (ddb.value(key) != dup.value(key)) {
                        dbIsSuperset = false; // the value of this field changed in the update version
                    }
                }

                if (dbIsSuperset) {
                    dbContact.removeDetail(&ddb);
                    upContact.removeDetail(&dup);
                    break;
                }
            }
        }
    }

    // Now extract the delta from the update version
    upDetails = upContact.details();
    QList<QContactDetail> retn;
    foreach (const QContactDetail &det, upDetails) {
        if (det.definitionName() != QContactDisplayLabel::DefinitionName
                && det.definitionName() != QContactType::DefinitionName
                && (definitionMask.isEmpty() || definitionMask.contains(det.definitionName()))) {
            retn.append(det);
        }
    }

    delta->append(retn);
    return QContactManager::NoError;
}

/*
   This function is called when an aggregate contact is updated.
   Instead of just saving changes to the aggregate, we save the
   changes (delta) to the 'local' contact (creating one if necessary)
   and promote all changes to the aggregate.
*/
QContactManager::Error ContactWriter::updateLocalAndAggregate(QContact *contact, const QStringList &definitionMask, bool withinTransaction)
{
    // 1) calculate the delta between "the new aggregate contact" and the "database aggregate contact"; bail out if no changes.
    // 2) get the contact which is aggregated by the aggregate contact and has a 'local' sync target
    // 3) if it exists, go to (5)
    // 4) create a new contact with 'local' sync target and the name copied from the aggregate
    // 5) save the "delta details" into the local sync target
    // 6) clobber the database aggregate by overwriting with the new aggregate.

    QList<QContactDetail> deltaDetails;
    QContactManager::Error deltaError = calculateDelta(m_reader, contact, definitionMask, &deltaDetails);
    if (deltaError != QContactManager::NoError) {
        qWarning() << "Unable to calculate delta for modified aggregate";
        return deltaError;
    }

    if (deltaDetails.isEmpty()) {
        return QContactManager::NoError; // nothing to do.
    }

    m_findLocalForAggregate.bindValue(":aggregateId", contact->id().localId() - 1);
    if (!m_findLocalForAggregate.exec()) {
        qWarning() << "Unable to query local for aggregate during update";
        qWarning() << m_findLocalForAggregate.lastError();
        return QContactManager::UnspecifiedError;
    }

    bool createdNewLocal = false;
    QContact localContact;
    if (m_findLocalForAggregate.next()) {
        // found the existing local contact aggregated by this aggregate.
        QList<QContact> readList;
        QList<QContactLocalId> whichList;
        whichList.append(m_findLocalForAggregate.value(0).toUInt() + 1);
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("UpdateAggregate"), &readList, whichList, QStringList());
        if (readError != QContactManager::NoError || readList.size() == 0) {
            qWarning() << "Unable to read local contact for aggregate" << contact->displayLabel() << "during update";
            return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
        }

        localContact = readList.at(0);
    } else {
        // no local contact exists for the aggregate.  Create a new one.
        createdNewLocal = true;
        QContactSyncTarget lst;
        lst.setSyncTarget(QLatin1String("local"));
        localContact.saveDetail(&lst);
        QContactName lcn = contact->detail<QContactName>();
        localContact.saveDetail(&lcn);
    }

    for (int i = 0; i < deltaDetails.size(); ++i) {
        QContactDetail det(deltaDetails.at(i));
        if (det.definitionName() == QContactGuid::DefinitionName
                || det.definitionName() == QContactSyncTarget::DefinitionName
                || det.definitionName() == QContactDisplayLabel::DefinitionName) {
            continue; // don't save these details.  Guid MUST be globally unique.
        }

        // handle unique details specifically.
        if (det.definitionName() == QContactName::DefinitionName) {
            QContactName lcn = localContact.detail<QContactName>();
            lcn.setPrefix(det.value(QContactName::FieldPrefix));
            lcn.setFirstName(det.value(QContactName::FieldFirstName));
            lcn.setMiddleName(det.value(QContactName::FieldMiddleName));
            lcn.setLastName(det.value(QContactName::FieldLastName));
            lcn.setSuffix(det.value(QContactName::FieldSuffix));
            lcn.setCustomLabel(det.value(QContactName::FieldCustomLabel));
            localContact.saveDetail(&lcn);
        } else if (det.definitionName() == QContactTimestamp::DefinitionName) {
            QContactTimestamp lts = localContact.detail<QContactTimestamp>();
            lts.setLastModified(det.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp));
            lts.setCreated(det.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp));
            localContact.saveDetail(&lts);
        } else if (det.definitionName() == QContactGender::DefinitionName) {
            QContactGender lg = localContact.detail<QContactGender>();
            lg.setGender(det.value(QContactGender::FieldGender));
            localContact.saveDetail(&lg);
        } else if (det.definitionName() == QContactFavorite::DefinitionName) {
            QContactFavorite lf = localContact.detail<QContactFavorite>();
            lf.setFavorite(det.value<bool>(QContactFavorite::FieldFavorite));
            localContact.saveDetail(&lf);
        } else {
            // other details can be duplicates.
            localContact.saveDetail(&det);
        }
    }

    // update (or create) the local contact
    QMap<int, QContactManager::Error> errorMap;
    QList<QContact> writeList;
    writeList.append(localContact);
    QContactManager::Error writeError = save(&writeList, QStringList(),     // when we update the local, we don't use definitionMask.
                                             &errorMap, withinTransaction,  // because it might be a new contact and need name+synct.
                                             false);
    if (writeError != QContactManager::NoError) {
        qWarning() << "Unable to update (or create) local contact for modified aggregate";
        return writeError;
    }

    // update (via clobber) the aggregate contact
    errorMap.clear();
    writeList.clear();
    writeList.append(*contact);
    writeError = save(&writeList, definitionMask, &errorMap, withinTransaction, true); // we're updating the aggregate contact deliberately.
    if (writeError != QContactManager::NoError) {
        qWarning() << "Unable to update modified aggregate";
        if (createdNewLocal) {
            QList<QContactLocalId> removeList;
            removeList.append(localContact.id().localId());
            QContactManager::Error removeError = remove(removeList, &errorMap, withinTransaction);
            if (removeError != QContactManager::NoError) {
                qWarning() << "Unable to remove stale local contact created for modified aggregate";
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

static QStringList getUnupdatedDetailNames()
{
    // THe list of definition names for details that are not updated in regeneration
    QStringList rv;
    rv << QContactSyncTarget::DefinitionName
       << QContactGuid::DefinitionName
       << QContactType::DefinitionName
       << QContactDisplayLabel::DefinitionName;
    return rv;
}

/*
    For every detail in a contact \a c, this function will check to see if an
    identical detail already exists in the \a aggregate contact.  If not, the
    detail from \a c will be "promoted" (saved in) the \a aggregate contact.

    Note that QContactSyncTarget and QContactGuid details will NOT be promoted,
    nor will QContactDisplayLabel or QContactType details.
*/
static void promoteDetailsToAggregate(const QContact &c, QContact *aggregate, const QStringList &definitionMask)
{
    static const QStringList unupdatedDetailNames(getUnupdatedDetailNames());

    QList<QContactDetail> currDetails = c.details();
    for (int j = 0; j < currDetails.size(); ++j) {
        QContactDetail currDet = currDetails.at(j);
        if (unupdatedDetailNames.contains(currDet.definitionName())) {
            // don't promote this detail.
            continue;
        }
        if (!definitionMask.isEmpty() && !definitionMask.contains(currDet.definitionName())) {
            // skip this detail
            continue;
        }

        // promote this detail to the aggregate.  Depending on uniqueness,
        // this consists either of composition or duplication.
        // Note: Composed (unique) details won't have any detailUri!
        if (currDet.definitionName() == QContactName::DefinitionName) {
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
            if (!cname.customLabel().isEmpty() && aname.customLabel().isEmpty())
                aname.setCustomLabel(cname.customLabel());
            aggregate->saveDetail(&aname);
        } else if (currDet.definitionName() == QContactTimestamp::DefinitionName) {
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
        } else if (currDet.definitionName() == QContactGender::DefinitionName) {
            // gender involves composition
            QContactGender cg(currDet);
            QContactGender ag(aggregate->detail<QContactGender>());
            if (!cg.gender().isEmpty() && ag.gender().isEmpty()) {
                ag.setGender(cg.gender());
                aggregate->saveDetail(&ag);
            }
        } else if (currDet.definitionName() == QContactFavorite::DefinitionName) {
            // favorite involves composition
            QContactFavorite cf(currDet);
            QContactFavorite af(aggregate->detail<QContactFavorite>());
            if (cf.isFavorite() && !af.isFavorite()) {
                af.setFavorite(true);
                aggregate->saveDetail(&af);
            }
        } else {
            // All other details involve duplication.
            // Only duplicate from c to a if an identical detail doesn't already exist in a.
            // We also modify any detail uris by prepending "aggregate:" to the start,
            // to ensure uniqueness.
            adjustDetailUrisForAggregate(currDet);

            // This is a pretty crude heuristic.  The detail equality
            // algorithm only attempts to match values, not key/value pairs.
            // XXX TODO: use a better heuristic to minimise duplicates.
            bool needsPromote = true;
            QList<QContactDetail> allADetails = aggregate->details();
            foreach (const QContactDetail &ad, allADetails) {
                if (currDet == ad) {
                    needsPromote = false;
                    break;
                }
            }

            if (needsPromote) {
                aggregate->saveDetail(&currDet);
            }
        }
    }
}

/*
    Basic match/null/conflict detail detection
*/
#define CLAMP_MATCH_LIKELIHOOD(value) (value > 10) ? 10 : (value < 0) ? 0 : value
template<typename T>
int detailMatch(const QContact &first, const QContact &second)
{
    QList<T> fdets = first.details<T>();
    QList<T> sdets = second.details<T>();

    if (fdets.size() == 0 || sdets.size() == 0)
        return 0; // no matches, but no conflicts.

    foreach (const T &ft, fdets) {
        foreach (const T &st, sdets) {
            if (ft == st) {
                return 1; // match!
            }
        }
    }

    return -1; // conflict!
}

/*
    Returns a value from 0 to 10 inclusive which defines the likelihood that
    the \a first contact refers to the same real-life entity as the \a second
    contact.  This function will return 10 if the contacts are definitely the
    same, and 0 if they are definitely different.

    The criteria is as follows:

    10) The first and last name match exactly AND either the phone number
        matches OR the email address matches.
    9)  The last name matches exactly and the first name matches by fragment
        AND either the phone number matches OR the email address matches.
    8)  The last name matches exactly and the first name matches by fragment,
        AND the phone number doesn't match BUT no email address exists AND
        at least one online account address matches
    7)  The last name matches exactly and the first name matches by fragment,
        AND the phone number doesn't match BUT no email address exists AND
        no contradictory online account addresses exist (eg, different MSN
        addresses).
    7)  The last name of one is empty, and the first name matches exactly,
        AND the phone number matches OR the email address matches.
    6)  ...

    Note: this function will always return 0 if there exists an "IsNot"
    relationship between the two (which must be manually added if the user
    manually unlinks the contacts).
*/
static int matchLikelihood(const QContact &contact, const QContact &aggregator)
{
    int retn = 10;

    QList<QContactRelationship> srels = aggregator.relationships(QLatin1String("IsNot")); // special "Unlink" relationship
    foreach (const QContactRelationship &r, srels) {
        if (r.first() == contact.id() || r.second() == contact.id()) {
            return 0; // have a manually added "IsNot" relationship between them.
        }
    }


    // XXX TODO: Should also load each of the contacts Aggregated by the aggregator.
    // If any of them have the same sync target as this contact, then we should also
    // return zero.


    // do heuristic matching
    QContactName fName = contact.detail<QContactName>();
    QContactName sName = aggregator.detail<QContactName>();
    if (!fName.lastName().isEmpty() && !sName.lastName().isEmpty()
            && fName.lastName().toLower() == sName.lastName().toLower()) {
        // last name matches.  no reduction in match likelihood.
    } else if (fName.lastName().isEmpty() || sName.lastName().isEmpty()) {
        retn -= 2; // at least one of them has no last name.  Decrease likelihood, but still possible match
    } else {
        retn -= 6; // last names don't match.  Drastically decrease likelihood.
    }

    if (!fName.firstName().isEmpty() && !sName.firstName().isEmpty()
            && fName.firstName().toLower() == sName.firstName().toLower()) {
        // first name matches.  no reduction in match likelihood.
    } else if (!fName.firstName().isEmpty() && !sName.firstName().isEmpty()
            && (fName.firstName().toLower().startsWith(sName.firstName().toLower())
            || sName.firstName().toLower().startsWith(fName.firstName().toLower()))) {
        retn -= 1; // first name has start-fragment match
    } else {
        retn -= 3; // first name doesn't match.
    }

    int phMatch = detailMatch<QContactPhoneNumber>(contact, aggregator);
    int emMatch = detailMatch<QContactEmailAddress>(contact, aggregator);
    int oaMatch = detailMatch<QContactOnlineAccount>(contact, aggregator);
    retn += oaMatch; // if an online account matches, improve our likelihood.
    if (phMatch > 0)
        return CLAMP_MATCH_LIKELIHOOD(retn); // matched phone number - whatever the current match likelihood is, return it.
    if (emMatch > 0)
        return CLAMP_MATCH_LIKELIHOOD(retn); // matched email address - whatever the current match likelihood is, return it.

    if (phMatch == 0 && emMatch == 0)
        retn -= 1; // no matches, but no conflicts
    if (phMatch < 0)
        retn -= 2; // conflicting phone number
    if (emMatch < 0)
        retn -= 2; // conflicting email address

    return (CLAMP_MATCH_LIKELIHOOD(retn));
}

/*
    Searches through the list of aggregate contacts \a aggregates for a
    contact which represents the same real-life entity as the contact \a c.

    If such a contact is found, it sets the \a index to the index into the
    list at which it was found, and returns that contact.

    If no such contact is found, it will return a new, empty contact and
    \a index will be set to -1.
*/
static QContact findMatch(const QContact &c, const QList<QContact> &aggregates, int *index)
{
    static const int MATCH_THRESHOLD = 7;
    for (int i = 0; i < aggregates.size(); ++i) {
        QContact a = aggregates.at(i);
        if (matchLikelihood(c, a) >= MATCH_THRESHOLD) {
            *index = i;
            return a;
        }
    }

    *index = -1;
    return QContact();
}

/*
   This function is called when a new contact is created.  The
   aggregate contacts are searched for a match, and the matching
   one updated if it exists; or a new aggregate is created.
*/
QContactManager::Error ContactWriter::updateOrCreateAggregate(QContact *contact, const QStringList &definitionMask, bool withinTransaction)
{
    // 1) read all aggregates
    // 2) search for match
    // 3) if exists, update the existing aggregate (by default, non-clobber:
    //    only update empty fields of details, or promote non-existent details.  Never delete or replace details.)
    // 4) otherwise, create new aggregate, consisting of all details of contact, return.

    QList<QContact> allAggregates;
    QContactManager::Error err = m_reader->readContacts(QLatin1String("CreateAggregate"),
                                                        &allAggregates,
                                                        QContactFilter(),
                                                        QContactSortOrder(),
                                                        definitionMask);

    if (err != QContactManager::NoError) {
        qWarning() << "Could not read aggregate contacts during creation aggregation";
        return err;
    }

    QMap<int, QContactManager::Error> errorMap;
    QList<QContact> saveContactList;
    QList<QContactRelationship> saveRelationshipList;

    int index = -1;
    QContact matchingAggregate = findMatch(*contact, allAggregates, &index);
    bool found = index >= 0;

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
    err = save(&saveContactList, QStringList(), &errorMap, withinTransaction, true); // we're updating (or creating) the aggregate
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
    QContactRelationship r;
    r.setFirst(matchingAggregate.id());
    r.setSecond(contact->id());
    r.setRelationshipType(QContactRelationship::Aggregates);
    saveRelationshipList.append(r);
    err = save(saveRelationshipList, &errorMap);
    if (err != QContactManager::NoError) {
        // if the aggregation relationship fails, the entire save has failed.
        qWarning() << "Unable to save aggregation relationship!";

        if (!found) {
            // clean up the newly created contact.
            QList<QContactLocalId> removeList;
            removeList.append(matchingAggregate.id().localId());
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
void ContactWriter::regenerateAggregates(const QList<QContactLocalId> &aggregateIds, const QStringList &definitionMask, bool withinTransaction)
{
    static const QStringList unupdatedDetailNames(getUnupdatedDetailNames());

    // for each aggregate contact:
    // 1) get the contacts it aggregates
    // 2) build unique details via composition (name / timestamp / gender / favorite - NOT synctarget or guid)
    // 3) append non-unique details
    // In all cases, we "prefer" the 'local' contact's data (if it exists)

    QList<QContact> aggregatesToSave;
    QList<QContactLocalId> aggregatesToSaveIds;
    foreach (QContactLocalId aggId, aggregateIds) {
        QList<QContactLocalId> readIds;
        readIds.append(aggId);
        m_findRelatedForAggregate.bindValue(":aggregateId", (aggId - 1));
        if (!m_findRelatedForAggregate.exec()) {
            qWarning() << "Failed to find related contacts for aggregate" << aggId << "during regenerate";
            continue;
        }
        while (m_findRelatedForAggregate.next()) {
            readIds.append(m_findRelatedForAggregate.value(0).toUInt() + 1);
        }

        if (readIds.size() == 1) { // only the aggregate?
            qWarning() << "Existing aggregate" << aggId << "should already have been removed - aborting regenerate";
            continue;
        }

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("RegenerateAggregate"),
                                                                  &readList, readIds, QStringList());
        if (readError != QContactManager::NoError
                || readList.size() <= 1
                || readList.at(0).detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != QLatin1String("aggregate")) {
            qWarning() << "Failed to read related contacts for aggregate" << aggId << "during regenerate";
            continue;
        }

        // We have read the related contacts from the database.  Now compose the aggregate.
        // Step one: throw away all current data, except for guid and synctarget.
        QContact aggregateContact = readList.at(0);
        QList<QContactDetail> allDetails = aggregateContact.details();
        for (int i = 0; i < allDetails.size(); ++i) {
            QContactDetail det = allDetails.at(i);
            if (!unupdatedDetailNames.contains(det.definitionName()) &&
                (definitionMask.isEmpty() || definitionMask.contains(det.definitionName()))) {
                aggregateContact.removeDetail(&det);
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
                if (!unupdatedDetailNames.contains(currDet.definitionName()) &&
                    (definitionMask.isEmpty() || definitionMask.contains(currDet.definitionName()))) {
                    // promote this detail to the aggregate.
                    adjustDetailUrisForAggregate(currDet);
                    aggregateContact.saveDetail(&currDet);
                }
            }

            // we save the updated aggregates to database all in a batch at the end.
            if (!aggregatesToSaveIds.contains(aggregateContact.id().localId())) {
                aggregatesToSave.append(aggregateContact);
                aggregatesToSaveIds.append(aggregateContact.id().localId());
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

            // we save the updated aggregates to database all in a batch at the end.
            if (!aggregatesToSaveIds.contains(aggregateContact.id().localId())) {
                aggregatesToSave.append(aggregateContact);
                aggregatesToSaveIds.append(aggregateContact.id().localId());
            }
        }
    }

    QMap<int, QContactManager::Error> errorMap;
    QContactManager::Error writeError = save(&aggregatesToSave, QStringList(), &errorMap, withinTransaction, true); // we're updating aggregates.
    if (writeError != QContactManager::NoError) {
        qWarning() << "Failed to write updated aggregate contacts during regenerate";
        qWarning() << "definitionMask:" << definitionMask;
    }
}

QContactManager::Error ContactWriter::create(QContact *contact, const QStringList &definitionMask, bool withinTransaction)
{
#ifndef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    Q_UNUSED(withinTransaction)
#endif
    bindContactDetails(*contact, m_insertContact);
    if (!m_insertContact.exec()) {
        qWarning() << "Failed to create contact";
        qWarning() << m_insertContact.lastError();
        return QContactManager::UnspecifiedError;
    } else {
        QContactLocalId contactId = m_insertContact.lastInsertId().toUInt();
        QContactManager::Error writeErr = write(contactId, contact, definitionMask);

        if (writeErr == QContactManager::NoError) {
            // successfully saved all data.  Update id.
            QContactId id;
            id.setLocalId(contactId + 1);
            id.setManagerUri(QLatin1String("org.nemomobile.contacts.sqlite"));
            contact->setId(id);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
            // and either update the aggregate contact (if it exists) or create a new one (unless it is an aggregate contact).
            if (contact->detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != QLatin1String("aggregate")) {
                writeErr = updateOrCreateAggregate(contact, definitionMask, withinTransaction);
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
        }

        return writeErr;
    }
}

QContactManager::Error ContactWriter::update(QContact *contact, const QStringList &definitionMask, bool withinTransaction, bool withinAggregateUpdate)
{
#ifndef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    Q_UNUSED(withinTransaction)
    Q_UNUSED(withinAggregateUpdate)
#endif
    // 0 is an invalid QContactLocalId but a valid sqlite row id, so ids are all offset by one.
    QContactLocalId contactId = contact->localId() - 1;

    m_checkContactExists.bindValue(0, contactId);
    if (!m_checkContactExists.exec()) {
        qWarning() << "Failed to check contact existence";
        qWarning() << m_checkContactExists.lastError();
        return QContactManager::UnspecifiedError;
    }
    m_checkContactExists.next();
    int exists = m_checkContactExists.value(0).toInt();
    if (!exists)
        return QContactManager::DoesNotExistError;
    QString oldSyncTarget = m_checkContactExists.value(1).toString();
    QString newSyncTarget = contact->detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget);

    if (newSyncTarget != oldSyncTarget && oldSyncTarget != QLatin1String("local")) {
        // they are attempting to manually change the sync target value of a non-local contact
        qWarning() << "Cannot manually change sync target!";
        return QContactManager::InvalidDetailError;
    }

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

    // else, just update the contact as normal.  Note that we
    // don't regenerate the aggregate contact, as we only do
    // that when we create a new contact, not when we update
    // an existing one.
#endif

    bindContactDetails(*contact, m_updateContact);
    m_updateContact.bindValue(12, contactId);

    if (!m_updateContact.exec()) {
        qWarning() << "Failed to update contact";
        qWarning() << m_updateContact.lastError();
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error writeError = write(contactId, contact, definitionMask);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (writeError == QContactManager::NoError) {
        if (!withinAggregateUpdate && oldSyncTarget != QLatin1String("aggregate")) {
            QList<QContactLocalId> aggregatesOfUpdated;
            m_findAggregateForLocal.bindValue(":localId", contactId);
            if (!m_findAggregateForLocal.exec()) {
                qWarning() << "Failed to fetch aggregator contact ids during remove";
                qWarning() << m_findAggregateForLocal.lastError();
                return QContactManager::UnspecifiedError;
            }
            while (m_findAggregateForLocal.next()) {
                aggregatesOfUpdated.append(m_findAggregateForLocal.value(0).toUInt() + 1);
            }

            if (aggregatesOfUpdated.size() > 0) {
                regenerateAggregates(aggregatesOfUpdated, definitionMask, withinTransaction);
            }
        }
    }
#endif

    return writeError;
}

QContactManager::Error ContactWriter::write(QContactLocalId contactId, QContact *contact, const QStringList &definitionMask)
{
    QContactManager::Error error = QContactManager::NoError;

    // look for unsupported detail data.  XXX TODO: this is really slow, due to string comparison.
    // We could simply ignore all unsupported data during save, which would save quite some time.
    QList<QContactDetail> allDets = contact->details();
    foreach (const QContactDetail &det, allDets) {
        if (det.definitionName() != QContactType::DefinitionName
                && det.definitionName() != QContactDisplayLabel::DefinitionName
                && det.definitionName() != QContactName::DefinitionName
                && det.definitionName() != QContactSyncTarget::DefinitionName
                && det.definitionName() != QContactGuid::DefinitionName
                && det.definitionName() != QContactNickname::DefinitionName
                && det.definitionName() != QContactFavorite::DefinitionName
                && det.definitionName() != QContactGender::DefinitionName
                && det.definitionName() != QContactTimestamp::DefinitionName
                && det.definitionName() != QContactPhoneNumber::DefinitionName
                && det.definitionName() != QContactEmailAddress::DefinitionName
                && det.definitionName() != QContactBirthday::DefinitionName
                && det.definitionName() != QContactAvatar::DefinitionName
                && det.definitionName() != QContactOnlineAccount::DefinitionName
                && det.definitionName() != QContactPresence::DefinitionName
                && det.definitionName() != QContactGlobalPresence::DefinitionName
                && det.definitionName() != QContactTpMetadata::DefinitionName
                && det.definitionName() != QContactAddress::DefinitionName
                && det.definitionName() != QContactTag::DefinitionName
                && det.definitionName() != QContactUrl::DefinitionName
                && det.definitionName() != QContactAnniversary::DefinitionName
                && det.definitionName() != QContactHobby::DefinitionName
                && det.definitionName() != QContactNote::DefinitionName
                && det.definitionName() != QContactOrganization::DefinitionName
                && det.definitionName() != QContactRingtone::DefinitionName) {
            return QContactManager::InvalidDetailError;
        }
    }

    if (writeDetails<QContactAddress>(contactId, contact, m_removeAddress, definitionMask, &error)
            && writeDetails<QContactAnniversary>(contactId, contact, m_removeAnniversary, definitionMask, &error)
            && writeDetails<QContactAvatar>(contactId, contact, m_removeAvatar, definitionMask, &error)
            && writeDetails<QContactBirthday>(contactId, contact, m_removeBirthday, definitionMask, &error)
            && writeDetails<QContactEmailAddress>(contactId, contact, m_removeEmailAddress, definitionMask, &error)
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
            && writeDetails<QContactTpMetadata>(contactId, contact, m_removeTpMetadata, definitionMask, &error)) {
        return QContactManager::NoError;
    }
    return error;
}

void ContactWriter::bindContactDetails(const QContact &contact, QSqlQuery &query)
{
    query.bindValue(0, contact.displayLabel());

    QContactName name = contact.detail<QContactName>();
    query.bindValue(1, name.variantValue(QContactName::FieldFirstName));
    query.bindValue(2, name.variantValue(QContactName::FieldLastName));
    query.bindValue(3, name.variantValue(QContactName::FieldMiddleName));
    query.bindValue(4, name.variantValue(QContactName::FieldPrefix));
    query.bindValue(5, name.variantValue(QContactName::FieldSuffix));
    query.bindValue(6, name.variantValue(QContactName::FieldCustomLabel));

    QContactSyncTarget starget = contact.detail<QContactSyncTarget>();
    QString stv = starget.syncTarget();
    if (stv.isEmpty())
        stv = QLatin1String("local"); // by default, it is a "local device" contact.
    query.bindValue(7, stv);

    QContactTimestamp timestamp = contact.detail<QContactTimestamp>();
    query.bindValue(8, timestamp.variantValue(QContactTimestamp::FieldCreationTimestamp));
    query.bindValue(9, timestamp.variantValue(QContactTimestamp::FieldModificationTimestamp));

    QContactGender gender = contact.detail<QContactGender>();
    query.bindValue(10, gender.variantValue(QContactGender::FieldGender));

    QContactFavorite favorite = contact.detail<QContactFavorite>();
    query.bindValue(11, favorite.isFavorite());
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactAddress &detail)
{
    typedef QContactAddress T;
    m_insertAddress.bindValue(0, contactId);
    m_insertAddress.bindValue(1, detail.variantValue(T::FieldStreet));
    m_insertAddress.bindValue(2, detail.variantValue(T::FieldPostOfficeBox));
    m_insertAddress.bindValue(3, detail.variantValue(T::FieldRegion));
    m_insertAddress.bindValue(4, detail.variantValue(T::FieldLocality));
    m_insertAddress.bindValue(5, detail.variantValue(T::FieldPostcode));
    m_insertAddress.bindValue(6, detail.variantValue(T::FieldCountry));
    return m_insertAddress;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactAnniversary &detail)
{
    typedef QContactAnniversary T;
    m_insertAnniversary.bindValue(0, contactId);
    m_insertAnniversary.bindValue(1, detail.variantValue(T::FieldOriginalDate));
    m_insertAnniversary.bindValue(2, detail.variantValue(T::FieldCalendarId));
    m_insertAnniversary.bindValue(3, detail.variantValue(T::FieldSubType));
    return m_insertAnniversary;
}


QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactAvatar &detail)
{
    typedef QContactAvatar T;
    m_insertAvatar.bindValue(0, contactId);
    m_insertAvatar.bindValue(1, detail.variantValue(T::FieldImageUrl));
    m_insertAvatar.bindValue(2, detail.variantValue(T::FieldVideoUrl));
    m_insertAvatar.bindValue(3, detail.variantValue(QLatin1String("AvatarMetadata")));
    return m_insertAvatar;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactBirthday &detail)
{
    typedef QContactBirthday T;
    m_insertBirthday.bindValue(0, contactId);
    m_insertBirthday.bindValue(1, detail.variantValue(T::FieldBirthday));
    m_insertBirthday.bindValue(2, detail.variantValue(T::FieldCalendarId));
    return m_insertBirthday;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactEmailAddress &detail)
{
    typedef QContactEmailAddress T;
    m_insertEmailAddress.bindValue(0, contactId);
    m_insertEmailAddress.bindValue(1, detail.variantValue(T::FieldEmailAddress));
    return m_insertEmailAddress;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactGuid &detail)
{
    typedef QContactGuid T;
    m_insertGuid.bindValue(0, contactId);
    m_insertGuid.bindValue(1, detail.variantValue(T::FieldGuid));
    return m_insertGuid;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactHobby &detail)
{
    typedef QContactHobby T;
    m_insertHobby.bindValue(0, contactId);
    m_insertHobby.bindValue(1, detail.variantValue(T::FieldHobby));
    return m_insertHobby;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactNickname &detail)
{
    typedef QContactNickname T;
    m_insertNickname.bindValue(0, contactId);
    m_insertNickname.bindValue(1, detail.variantValue(T::FieldNickname));
    return m_insertNickname;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactNote &detail)
{
    typedef QContactNote T;
    m_insertNote.bindValue(0, contactId);
    m_insertNote.bindValue(1, detail.variantValue(T::FieldNote));
    return m_insertNote;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactOnlineAccount &detail)
{
    typedef QContactOnlineAccount T;
    m_insertOnlineAccount.bindValue(0, contactId);
    m_insertOnlineAccount.bindValue(1, detail.variantValue(T::FieldAccountUri));
    m_insertOnlineAccount.bindValue(2, detail.variantValue(T::FieldProtocol));
    m_insertOnlineAccount.bindValue(3, detail.variantValue(T::FieldServiceProvider));
    m_insertOnlineAccount.bindValue(4, detail.variantValue(T::FieldCapabilities));
    m_insertOnlineAccount.bindValue(5, detail.subTypes().join(QLatin1String(";")));
    m_insertOnlineAccount.bindValue(6, detail.variantValue("AccountPath"));
    m_insertOnlineAccount.bindValue(7, detail.variantValue("AccountIconPath"));
    m_insertOnlineAccount.bindValue(8, detail.variantValue("Enabled"));
    return m_insertOnlineAccount;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactOrganization &detail)
{
    typedef QContactOrganization T;
    m_insertOrganization.bindValue(0, contactId);
    m_insertOrganization.bindValue(1, detail.variantValue(T::FieldName));
    m_insertOrganization.bindValue(2, detail.variantValue(T::FieldRole));
    m_insertOrganization.bindValue(3, detail.variantValue(T::FieldTitle));
    m_insertOrganization.bindValue(4, detail.variantValue(T::FieldLocation));
    m_insertOrganization.bindValue(5, detail.department().join(QLatin1String(";")));
    m_insertOrganization.bindValue(6, detail.variantValue(T::FieldLogoUrl));
    return m_insertOrganization;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactPhoneNumber &detail)
{
    typedef QContactPhoneNumber T;
    m_insertPhoneNumber.bindValue(0, contactId);
    m_insertPhoneNumber.bindValue(1, detail.variantValue(T::FieldNumber));
    m_insertPhoneNumber.bindValue(2, detail.subTypes().join(QLatin1String(";")));
    m_insertPhoneNumber.bindValue(3, detail.variantValue("NormalizedNumber"));
    return m_insertPhoneNumber;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactPresence &detail)
{
    typedef QContactPresence T;
    m_insertPresence.bindValue(0, contactId);
    m_insertPresence.bindValue(1, detail.variantValue(T::FieldPresenceState));
    m_insertPresence.bindValue(2, detail.variantValue(T::FieldTimestamp));
    m_insertPresence.bindValue(3, detail.variantValue(T::FieldNickname));
    m_insertPresence.bindValue(4, detail.variantValue(T::FieldCustomMessage));
    return m_insertPresence;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactRingtone &detail)
{
    typedef QContactRingtone T;
    m_insertRingtone.bindValue(0, contactId);
    m_insertRingtone.bindValue(1, detail.variantValue(T::FieldAudioRingtoneUrl));
    m_insertRingtone.bindValue(2, detail.variantValue(T::FieldVideoRingtoneUrl));
    return m_insertRingtone;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactTag &detail)
{
    typedef QContactTag T;
    m_insertTag.bindValue(0, contactId);
    m_insertTag.bindValue(1, detail.variantValue(T::FieldTag));
    return m_insertTag;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactUrl &detail)
{
    typedef QContactUrl T;
    m_insertUrl.bindValue(0, contactId);
    m_insertUrl.bindValue(1, detail.variantValue(T::FieldUrl));
    m_insertUrl.bindValue(2, detail.variantValue(T::FieldSubType));
    return m_insertUrl;
}

QSqlQuery &ContactWriter::bindDetail(QContactLocalId contactId, const QContactTpMetadata &detail)
{
    m_insertTpMetadata.bindValue(0, contactId);
    m_insertTpMetadata.bindValue(1, detail.variantValue("ContactId"));
    m_insertTpMetadata.bindValue(2, detail.variantValue("AccountId"));
    m_insertTpMetadata.bindValue(3, detail.variantValue("AccountEnabled"));
    return m_insertTpMetadata;
}
