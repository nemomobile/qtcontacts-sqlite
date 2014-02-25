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
#include "trace_p.h"

#include "../extensions/qcontactdeactivated.h"
#include "../extensions/qcontactincidental.h"
#include "../extensions/qcontactstatusflags.h"

#include <QContactFavorite>
#include <QContactGender>
#include <QContactGlobalPresence>
#include <QContactName>
#include <QContactSyncTarget>
#include <QContactTimestamp>
#include <QContactExtendedDetail>
#include <QContactFamily>
#include <QContactGeoLocation>
#include <QContactVersion>

#include <QSqlError>
#include <QUuid>

#include <algorithm>

#include <QtDebug>

using namespace Conversion;

static const QString aggregateSyncTarget(QString::fromLatin1("aggregate"));
static const QString localSyncTarget(QString::fromLatin1("local"));
static const QString wasLocalSyncTarget(QString::fromLatin1("was_local"));

static const QString aggregationIdsTable(QString::fromLatin1("aggregationIds"));
static const QString modifiableContactsTable(QString::fromLatin1("modifiableContacts"));
static const QString syncConstituentsTable(QString::fromLatin1("syncConstituents"));
static const QString syncAggregatesTable(QString::fromLatin1("syncAggregates"));

static const QString possibleAggregatesTable(QString::fromLatin1("possibleAggregates"));
static const QString matchEmailAddressesTable(QString::fromLatin1("matchEmailAddresses"));
static const QString matchPhoneNumbersTable(QString::fromLatin1("matchPhoneNumbers"));
static const QString matchOnlineAccountsTable(QString::fromLatin1("matchOnlineAccounts"));

static const char *findConstituentsForAggregate =
        "\n SELECT secondId FROM Relationships WHERE firstId = :aggregateId AND type = 'Aggregates'";

static const char *findConstituentsForAggregateIds =
        "\n SELECT Relationships.secondId"
        "\n FROM Relationships"
        "\n JOIN temp.aggregationIds ON Relationships.firstId = temp.aggregationIds.contactId"
        "\n WHERE Relationships.type = 'Aggregates'";

static const char *findLocalForAggregate =
        "\n SELECT DISTINCT Contacts.contactId"
        "\n FROM Contacts"
        "\n JOIN Relationships ON Relationships.secondId = Contacts.contactId"
        "\n WHERE Contacts.syncTarget = 'local'"
        "\n AND Relationships.firstId = :aggregateId"
        "\n AND Relationships.type = 'Aggregates'";

static const char *findAggregateForContact =
        "\n SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates' AND secondId = :localId";

static const char *findAggregateForContactIds =
        "\n SELECT DISTINCT Relationships.firstId"
        "\n FROM Relationships"
        "\n JOIN temp.aggregationIds ON Relationships.secondId = temp.aggregationIds.contactId"
        "\n WHERE Relationships.type = 'Aggregates'";

/*
    Aggregation heuristic.

    Search existing aggregate contacts, for matchability.
    The aggregate with the highest match score (over the threshold)
    represents the same "actual person".
    The newly saved contact then becomes a constituent of that
    aggregate.

    Note that individual contacts from the same syncTarget can
    represent the same actual person (eg, Telepathy might provide
    buddies from different Jabber servers/rosters and thus if
    you have the same buddy on multiple services, they need to
    be aggregated together.

    Stages:
    1) select all possible aggregate ids
    2) join those ids on the tables of interest to get the data we match against
    3) perform the heuristic matching, ordered by "best score"
    4) select highest score; if over threshold, select that as aggregate.
*/
static const char *possibleAggregatesWhere = /* SELECT contactId FROM Contacts ... */
        "\n WHERE Contacts.syncTarget = 'aggregate'"
        "\n AND (COALESCE(Contacts.lowerLastName, '') = '' OR COALESCE(:lastName, '') = '' OR Contacts.lowerLastName = :lastName)"
        "\n AND COALESCE(Contacts.gender, '') != :excludeGender"
        "\n AND contactId > 2" // exclude self contact
        "\n AND isDeactivated = 0" // exclude deactivated
        "\n AND contactId NOT IN ("
        "\n   SELECT secondId FROM Relationships WHERE firstId = :contactId AND type = 'IsNot'"
        "\n   UNION"
        "\n   SELECT firstId FROM Relationships WHERE secondId = :contactId AND type = 'IsNot'"
        "\n )";

static const char *heuristicallyMatchData =
        "\n SELECT Matches.contactId, sum(Matches.score) AS total FROM ("
        "\n     SELECT Contacts.contactId, 20 AS score FROM Contacts"
        "\n     INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
        "\n         WHERE lowerLastName != '' AND lowerLastName = :lastName"
        "\n         AND lowerFirstName != '' AND :firstName != ''"
        "\n         AND ((lowerFirstName NOT LIKE '%-%' AND :firstName NOT LIKE '%-%' AND"
        "\n               (lowerFirstName LIKE ('%' || :firstName || '%') OR :firstName LIKE ('%' || lowerFirstName || '%'))) OR"
        "\n              lowerFirstName = :firstName)"
        "\n     UNION"
        "\n     SELECT Contacts.contactId, 15 AS score FROM Contacts"
        "\n     INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
        "\n         WHERE COALESCE(lowerFirstName, '') = '' AND COALESCE(:firstName,'') = ''"
        "\n         AND COALESCE(lowerLastName, '') = '' AND COALESCE(:lastName,'') = ''"
        "\n         AND EXISTS ("
        "\n             SELECT * FROM Nicknames"
        "\n             WHERE Nicknames.contactId = Contacts.contactId"
        "\n             AND lowerNickName = :nickname)"
        "\n     UNION"
        "\n     SELECT Contacts.contactId, 12 AS score FROM Contacts"
        "\n     INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
        "\n         WHERE (COALESCE(lowerLastName, '') = '' OR COALESCE(:lastName, '') = '')"
        "\n         AND lowerFirstName != ''"
        "\n         AND ((lowerFirstName NOT LIKE '%-%' AND :firstName NOT LIKE '%-%' AND"
        "\n               (lowerFirstName LIKE ('%' || :firstName || '%') OR :firstName LIKE ('%' || lowerFirstName || '%'))) OR"
        "\n              lowerFirstName = :firstName)"
        "\n     UNION"
        "\n     SELECT Contacts.contactId, 12 AS score FROM Contacts"
        "\n     INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
        "\n         WHERE lowerLastName != '' AND lowerLastName = :lastName"
        "\n         AND (COALESCE(lowerFirstName, '') = '' OR COALESCE(:firstName, '') = '')"
        "\n     UNION"
        "\n     SELECT EmailAddresses.contactId, 3 AS score FROM EmailAddresses"
        "\n     INNER JOIN temp.possibleAggregates ON EmailAddresses.contactId = temp.possibleAggregates.contactId"
        "\n     INNER JOIN temp.matchEmailAddresses ON EmailAddresses.lowerEmailAddress = temp.matchEmailAddresses.value"
        "\n     UNION"
        "\n     SELECT PhoneNumbers.contactId, 3 AS score FROM PhoneNumbers"
        "\n     INNER JOIN temp.possibleAggregates ON PhoneNumbers.contactId = temp.possibleAggregates.contactId"
        "\n     INNER JOIN temp.matchPhoneNumbers ON PhoneNumbers.normalizedNumber = temp.matchPhoneNumbers.value"
        "\n     UNION"
        "\n     SELECT OnlineAccounts.contactId, 3 AS score FROM OnlineAccounts"
        "\n     INNER JOIN temp.possibleAggregates ON OnlineAccounts.contactId = temp.possibleAggregates.contactId"
        "\n     INNER JOIN temp.matchOnlineAccounts ON OnlineAccounts.lowerAccountUri = temp.matchOnlineAccounts.value"
        "\n     UNION"
        "\n     SELECT Nicknames.contactId, 1 AS score FROM Nicknames"
        "\n     INNER JOIN temp.possibleAggregates ON Nicknames.contactId = temp.possibleAggregates.contactId"
        "\n         WHERE lowerNickName != '' AND lowerNickName = :nickname"
        "\n ) AS Matches"
        "\n GROUP BY Matches.contactId"
        "\n ORDER BY total DESC"
        "\n LIMIT 1";

static const char *selectAggregateContactIds =
        "\n SELECT Contacts.contactId"
        "\n FROM Contacts"
        "\n JOIN temp.aggregationIds ON Contacts.ContactId = temp.aggregationIds.ContactId"
        "\n WHERE Contacts.syncTarget = 'aggregate'";

static const char *childlessAggregateIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget = 'aggregate' AND contactId NOT IN ("
        "\n SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates')";

static const char *orphanContactIds =
        "\n SELECT contactId FROM Contacts WHERE syncTarget != 'aggregate' AND isDeactivated = 0 AND contactId NOT IN ("
        "\n SELECT DISTINCT secondId FROM Relationships WHERE type = 'Aggregates')";

static const char *countLocalConstituents =
        "\n SELECT COUNT(*) FROM Relationships"
        "\n JOIN Contacts ON Contacts.contactId = Relationships.secondId"
        "\n WHERE Relationships.firstId = :aggregateId"
        "\n AND Relationships.type = 'Aggregates'"
        "\n AND Contacts.syncTarget = 'local';";

static const char *updateSyncTarget =
        "\n UPDATE Contacts SET syncTarget = :syncTarget WHERE contactId = :contactId;";

static const char *checkContactExists =
        "\n SELECT COUNT(contactId), syncTarget FROM Contacts WHERE contactId = :contactId;";

static const char *existingContactIds =
        "\n SELECT DISTINCT contactId FROM Contacts;";

static const char *modifiableDetails =
        "\n SELECT provenance, detail, Details.contactId FROM Details"
        "\n JOIN temp.modifiableContacts ON temp.modifiableContacts.contactId = Details.contactId"
        "\n WHERE Details.modifiable = 1";

static const char *selfContactId =
        "\n SELECT DISTINCT contactId FROM Identities WHERE identity = :identity;";

static const char *syncContactIds =
        "\n SELECT DISTINCT Relationships.firstId"
        "\n FROM Relationships"
        "\n JOIN Contacts AS Aggregates ON Aggregates.contactId = Relationships.firstId"
        "\n JOIN Contacts AS Constituents ON Constituents.contactId = Relationships.secondId"
        "\n WHERE Relationships.type = 'Aggregates'"
        "\n AND Constituents.syncTarget = :syncTarget"
        "\n AND Aggregates.modified > :lastSync";

static const char *aggregateContactIds =
        "\n SELECT Relationships.firstId"
        "\n FROM Relationships"
        "\n JOIN Contacts ON Contacts.contactId = Relationships.firstId"
        "\n WHERE type = 'Aggregates' AND secondId IN ("
        "\n  SELECT contactId FROM temp.syncConstituents)"
        "\n AND Contacts.modified > :lastSync";

static const char *constituentContactDetails =
        "\n SELECT Relationships.firstId, Contacts.contactId, Contacts.syncTarget"
        "\n FROM Relationships"
        "\n JOIN Contacts ON Contacts.contactId = Relationships.secondId"
        "\n WHERE Relationships.type = 'Aggregates'"
        "\n AND Relationships.firstId IN ("
        "\n  SELECT contactId FROM temp.syncAggregates)"
        "\n AND Contacts.isDeactivated = 0";

static const char *localConstituentIds =
        "\n SELECT R2.secondId, R1.secondId"
        "\n FROM Relationships AS R1"
        "\n JOIN Relationships AS R2 ON R2.firstId = R1.firstId"
        "\n JOIN Contacts ON Contacts.contactId = R2.secondId"
        "\n WHERE R1.type = 'Aggregates' AND R1.secondId IN ("
        "\n  SELECT contactId FROM temp.syncConstituents)"
        "\n AND R2.type = 'Aggregates'"
        "\n AND Contacts.syncTarget = 'local'";

static const char *affectedSyncTargets =
        "\n SELECT Contacts.syncTarget"
        "\n FROM Contacts"
        "\n JOIN Relationships AS R1 ON Contacts.contactId = R1.secondId"
        "\n JOIN Relationships AS R2 ON R2.firstId = R1.firstId"
        "\n WHERE R2.type = 'Aggregates' AND R2.secondId IN ("
        "\n  SELECT contactId FROM temp.syncConstituents)"
        "\n AND R1.type = 'Aggregates'"
        "\n AND Contacts.syncTarget != 'local'"
        "\n AND Contacts.syncTarget != 'was_local'";

static const char *addedSyncContactIds =
        "\n SELECT Relationships.firstId"
        "\n FROM Contacts"
        "\n JOIN Relationships ON Relationships.secondId = Contacts.contactId"
        "\n WHERE Contacts.created > :lastSync"
        "\n AND Contacts.syncTarget = 'local'"
        "\n AND Contacts.isIncidental = 0"
        "\n AND Relationships.type = 'Aggregates'";

static const char *deletedSyncContactIds =
        "\n SELECT contactId, syncTarget"
        "\n FROM DeletedContacts"
        "\n WHERE deleted > :lastSync";

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
        "\n  isOnline,"
        "\n  isDeactivated,"
        "\n  isIncidental)"
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
        "\n  :isOnline,"
        "\n  :isDeactivated,"
        "\n  :isIncidental);";

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
        "\n  isOnline = CASE WHEN :valueKnown = 1 THEN :value ELSE isOnline END, "
        "\n  isDeactivated = CASE WHEN :valueKnown = 1 THEN :value ELSE isDeactivated END "
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
        "\n  country,"
        "\n  subTypes)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :street,"
        "\n  :postOfficeBox,"
        "\n  :region,"
        "\n  :locality,"
        "\n  :postCode,"
        "\n  :country,"
        "\n  :subTypes)";

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
        "\n  enabled,"
        "\n  accountDisplayName,"
        "\n  serviceProviderDisplayName)"
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
        "\n  :enabled,"
        "\n  :accountDisplayName,"
        "\n  :serviceProviderDisplayName)";

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

static const char *insertExtendedDetail =
        "\n INSERT INTO ExtendedDetails ("
        "\n  contactId,"
        "\n  name,"
        "\n  data)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :name,"
        "\n  :data)";

static const char *insertDetail =
        "\n INSERT INTO Details ("
        "\n  contactId,"
        "\n  detailId,"
        "\n  detail,"
        "\n  detailUri,"
        "\n  linkedDetailUris,"
        "\n  contexts,"
        "\n  accessConstraints,"
        "\n  provenance,"
        "\n  modifiable)"
        "\n VALUES ("
        "\n  :contactId,"
        "\n  :detailId,"
        "\n  :detail,"
        "\n  :detailUri,"
        "\n  :linkedDetailUris,"
        "\n  :contexts,"
        "\n  :accessConstraints,"
        "\n  :provenance,"
        "\n  :modifiable);";

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
    , m_childlessAggregateIds(prepare(childlessAggregateIds, database))
    , m_orphanContactIds(prepare(orphanContactIds, database))
    , m_countLocalConstituents(prepare(countLocalConstituents, database))
    , m_updateSyncTarget(prepare(updateSyncTarget, database))
    , m_checkContactExists(prepare(checkContactExists, database))
    , m_existingContactIds(prepare(existingContactIds, database))
    , m_selfContactId(prepare(selfContactId, database))
    , m_syncContactIds(prepare(syncContactIds, database))
    , m_addedSyncContactIds(prepare(addedSyncContactIds, database))
    , m_deletedSyncContactIds(prepare(deletedSyncContactIds, database))
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
    , m_insertExtendedDetail(prepare(insertExtendedDetail, database))
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
    , m_removeExtendedDetail(prepare("DELETE FROM ExtendedDetails WHERE contactId = :contactId;", database))
    , m_removeDetail(prepare("DELETE FROM Details WHERE contactId = :contactId AND detail = :detail;", database))
    , m_removeIdentity(prepare("DELETE FROM Identities WHERE identity = :identity;", database))
    , m_reader(reader)
{
    // These queries need the 'temp.aggregationIds' to exist to prepare
    if (ContactsDatabase::createTemporaryContactIdsTable(m_database, aggregationIdsTable, QVariantList())) {
        m_findConstituentsForAggregateIds = prepare(findConstituentsForAggregateIds, database);
        m_findAggregateForContactIds = prepare(findAggregateForContactIds, database);
        m_selectAggregateContactIds = prepare(selectAggregateContactIds, database);
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary %1 table").arg(aggregationIdsTable));
    }

    // These queries needs the 'temp.syncConstituents' to exist to prepare
    if (ContactsDatabase::createTemporaryContactIdsTable(m_database, syncConstituentsTable, QVariantList())) {
        m_aggregateContactIds = prepare(aggregateContactIds, database);
        m_localConstituentIds = prepare(localConstituentIds, database);
        m_affectedSyncTargets = prepare(affectedSyncTargets, database);
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary %1 table").arg(syncConstituentsTable));
    }

    // This query needs the 'temp.syncAggregates' to exist to prepare
    if (ContactsDatabase::createTemporaryContactIdsTable(m_database, syncAggregatesTable, QVariantList())) {
        m_constituentContactDetails = prepare(constituentContactDetails, database);
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary %1 table").arg(syncAggregatesTable));
    }

    // This query needs the 'temp.modifiableContacts' to exist to prepare
    if (ContactsDatabase::createTemporaryContactIdsTable(m_database, modifiableContactsTable, QVariantList())) {
        m_modifiableDetails = prepare(modifiableDetails, database);
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary %1 table").arg(modifiableContactsTable));
    }

    // This query needs the temporary aggregation tables to exist
    if (ContactsDatabase::createTemporaryContactIdsTable(m_database, possibleAggregatesTable, QVariantList())) {
        if (ContactsDatabase::createTemporaryValuesTable(m_database, matchEmailAddressesTable, QVariantList()) &&
            ContactsDatabase::createTemporaryValuesTable(m_database, matchPhoneNumbersTable, QVariantList()) &&
            ContactsDatabase::createTemporaryValuesTable(m_database, matchOnlineAccountsTable, QVariantList())) {
            m_heuristicallyMatchData = prepare(heuristicallyMatchData, database);
        } else {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary match tables"));
        }
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary %1 table").arg(possibleAggregatesTable));
    }
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
        if (ContactsDatabase::beginTransaction(m_database))
            return true;

        m_databaseMutex->unlock();
    }

    return false;
}

bool ContactWriter::commitTransaction()
{
    if (!m_changedLocalIds.isEmpty()) {
        // Find any sync targets affected by modified local contacts
        bool error(false);

        QVariantList ids;
        foreach (quint32 id, m_changedLocalIds) {
            ids.append(id);
        }

        ContactsDatabase::clearTemporaryContactIdsTable(m_database, syncConstituentsTable);

        if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, syncConstituentsTable, ids)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncConstituents temporary table"));
            error = true;
        } else if (!m_affectedSyncTargets.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch affected sync targets:\n%1")
                    .arg(m_affectedSyncTargets.lastError().text()));
            error = true;
        }
        while (!error && m_affectedSyncTargets.next()) {
            const QString st(m_affectedSyncTargets.value(0).toString());
            if (!m_suppressedSyncTargets.contains(st)) {
                m_changedSyncTargets.insert(st);
            }
        }
        m_affectedSyncTargets.finish();

        if (error) {
            rollbackTransaction();
            return false;
        }
    }

    m_suppressedSyncTargets.clear();
    m_changedLocalIds.clear();

    if (!ContactsDatabase::commitTransaction(m_database)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Commit error: %1").arg(m_database.lastError().text()));
        rollbackTransaction();
        return false;
    }

    if (m_databaseMutex->isLocked()) {
        m_databaseMutex->unlock();
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Lock error: no lock held on commit"));
    }

    if (!m_addedIds.isEmpty()) {
        ContactNotifier::contactsAdded(m_addedIds.toList());
        m_addedIds.clear();
    }
    if (!m_changedIds.isEmpty()) {
        ContactNotifier::contactsChanged(m_changedIds.toList());
        m_changedIds.clear();
    }
    if (!m_presenceChangedIds.isEmpty()) {
        ContactNotifier::contactsPresenceChanged(m_presenceChangedIds.toList());
        m_presenceChangedIds.clear();
    }
    if (!m_changedSyncTargets.isEmpty()) {
        ContactNotifier::syncContactsChanged(m_changedSyncTargets.toList());
        m_changedSyncTargets.clear();
    }
    if (!m_removedIds.isEmpty()) {
        ContactNotifier::contactsRemoved(m_removedIds.toList());
        m_removedIds.clear();
    }
    return true;
}

void ContactWriter::rollbackTransaction()
{
    ContactsDatabase::rollbackTransaction(m_database);
    if (m_databaseMutex->isLocked()) {
        m_databaseMutex->unlock();
    } else {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Lock error: no lock held on rollback"));
    }

    m_removedIds.clear();
    m_suppressedSyncTargets.clear();
    m_changedLocalIds.clear();
    m_changedSyncTargets.clear();
    m_presenceChangedIds.clear();
    m_changedIds.clear();
    m_addedIds.clear();
}

QContactManager::Error ContactWriter::setIdentity(
        ContactsDatabase::Identity identity, QContactId contactId)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

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
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (relationships.isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while saving relationships"));
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after relationship save"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

template<typename T>
QString relationshipString(T type)
{
    return type();
}

QContactManager::Error ContactWriter::saveRelationships(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap)
{
    static const QString uri(QString::fromLatin1("qtcontacts:org.nemomobile.contacts.sqlite:"));

    // in order to perform duplicate detection we build up the following datastructure.
    QMultiMap<quint32, QPair<QString, quint32> > bucketedRelationships; // first id to <type, second id>.
    {
        if (!m_existingRelationships.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch existing relationships for duplicate detection during insert:\n%1")
                    .arg(m_existingRelationships.lastError().text()));
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch existing contacts for validity detection during insert:\n%1")
                    .arg(m_existingContactIds.lastError().text()));
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

        QContactId first(relationship.first().id());
        QContactId second(relationship.second().id());

        const quint32 firstId = ContactId::databaseId(first);
        const quint32 secondId = ContactId::databaseId(second);
        const QString &type = relationship.relationshipType();

        if ((firstId == secondId)
                || (!first.managerUri().isEmpty() &&
                    !first.managerUri().startsWith(uri)
                   )
                || (!second.managerUri().isEmpty() &&
                    !second.managerUri().startsWith(uri)
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare multiple insert relationships query:\n%1\nQuery:\n%2")
                .arg(multiInsertQuery.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < realInsertions; ++i) {
        multiInsertQuery.bindValue(QString(QLatin1String(":firstId%1")).arg(QString::number(i)), firstIdsToBind.at(i));
        multiInsertQuery.bindValue(QString(QLatin1String(":secondId%1")).arg(QString::number(i)), secondIdsToBind.at(i));
        multiInsertQuery.bindValue(QString(QLatin1String(":type%1")).arg(QString::number(i)), typesToBind.at(i));
    }

    if (realInsertions > 0 && !multiInsertQuery.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert relationships:\n%1\nQuery:\n%2")
                .arg(multiInsertQuery.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    }

    if (invalidInsertions > 0) {
        return QContactManager::InvalidRelationshipError;
    }

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (!aggregatesAffected.isEmpty()) {
        QContactManager::Error writeError = regenerateAggregates(aggregatesAffected.toList(), DetailList(), true);
        if (writeError != QContactManager::NoError)
            return writeError;
    }
#endif

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (relationships.isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing relationships"));
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after relationship removal"));
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch existing relationships for duplicate detection during insert:\n%1")
                    .arg(m_existingRelationships.lastError().text()));
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare remove relationship:\n%1")
                    .arg(removeRelationship.lastError().text()));
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to remove relationship:\n%1")
                    .arg(removeRelationship.lastError().text()));
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
    QList<QContactId> removedIds;
    QContactManager::Error removeError = removeChildlessAggregates(&removedIds);
    if (removeError != QContactManager::NoError)
        return removeError;

    foreach (const QContactId &id, removedIds) {
        m_removedIds.insert(id);
        aggregatesAffected.remove(ContactId::databaseId(id));
    }

    if (!aggregatesAffected.isEmpty()) {
        QContactManager::Error writeError = regenerateAggregates(aggregatesAffected.toList(), DetailList(), true);
        if (writeError != QContactManager::NoError)
            return writeError;
    }

    // Some contacts may need to have new aggregates created
    QContactManager::Error aggregateError = aggregateOrphanedContacts(true);
    if (aggregateError != QContactManager::NoError)
        return aggregateError;
#endif

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(const QList<QContactId> &contactIds, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (contactIds.isEmpty())
        return QContactManager::NoError;

    // grab the self-contact id so we can avoid removing it.
    quint32 selfContactId = 0;
    m_selfContactId.bindValue(":identity", ContactsDatabase::SelfContactId);
    if (!m_selfContactId.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch self contact id during remove:\n%1")
                .arg(m_selfContactId.lastError().text()));
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch existing contact ids during remove:\n%1")
                .arg(m_existingContactIds.lastError().text()));
        return QContactManager::UnspecifiedError;
    }
    while (m_existingContactIds.next()) {
        existingContactIds.insert(m_existingContactIds.value(0).toUInt());
    }
    m_existingContactIds.finish();

    // determine which contacts we actually need to remove
    QContactManager::Error error = QContactManager::NoError;
    QList<QContactId> realRemoveIds;
    QVariantList boundRealRemoveIds;
    for (int i = 0; i < contactIds.size(); ++i) {
        QContactId currId = contactIds.at(i);
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing contacts"));
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.bindValue(QLatin1String(":contactId"), boundRealRemoveIds);
        if (!m_removeContact.execBatch()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to remove contacts:\n%1")
                    .arg(m_removeContact.lastError().text()));
            if (!withinTransaction) {
                // only rollback if we created a transaction.
                rollbackTransaction();
            }
            m_removeContact.finish();
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.finish();
        foreach (const QContactId &rrid, realRemoveIds) {
            m_removedIds.insert(rrid);
        }
        if (!withinTransaction && !commitTransaction()) {
            // only commit if we created a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit removal"));
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

    ContactsDatabase::clearTemporaryContactIdsTable(m_database, aggregationIdsTable);

    if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, aggregationIdsTable, boundRealRemoveIds)) {
        return QContactManager::UnspecifiedError;
    } else {
        // Use the temporary table for both queries
        if (!m_selectAggregateContactIds.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to select aggregate contact ids during remove:\n%1")
                    .arg(m_selectAggregateContactIds.lastError().text()));
            return QContactManager::UnspecifiedError;
        }
        while (m_selectAggregateContactIds.next()) {
            aggregatesToRemove.append(m_selectAggregateContactIds.value(0).toUInt());
        }
        m_selectAggregateContactIds.finish();

        if (!m_findAggregateForContactIds.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch aggregator contact ids during remove:\n%1")
                    .arg(m_findAggregateForContactIds.lastError().text()));
            return QContactManager::UnspecifiedError;
        }
        while (m_findAggregateForContactIds.next()) {
            aggregatesOfRemoved.append(m_findAggregateForContactIds.value(0).toUInt());
        }
        m_findAggregateForContactIds.finish();
    }

    QVariantList boundNonAggregatesToRemove;
    QVariantList boundAggregatesToRemove;
    foreach (const QContactId &rrid, realRemoveIds) {
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing contacts"));
        return QContactManager::UnspecifiedError;
    }

    // remove the non-aggregate contacts
    if (boundNonAggregatesToRemove.size() > 0) {
        m_removeContact.bindValue(QLatin1String(":contactId"), boundNonAggregatesToRemove);
        if (!m_removeContact.execBatch()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to removed non-aggregate contacts:\n%1")
                    .arg(m_removeContact.lastError().text()));
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            m_removeContact.finish();
            return QContactManager::UnspecifiedError;
        }

        m_removeContact.finish();
    }

    // remove the aggregate contacts - and any contacts they aggregate
    if (boundAggregatesToRemove.size() > 0) {
        ContactsDatabase::clearTemporaryContactIdsTable(m_database, aggregationIdsTable);

        if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, aggregationIdsTable, boundAggregatesToRemove)) {
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        } else {
            if (!m_findConstituentsForAggregateIds.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch contacts aggregated by removed aggregates:\n%1")
                        .arg(m_findConstituentsForAggregateIds.lastError().text()));
                if (!withinTransaction) {
                    // only rollback the transaction if we created it
                    rollbackTransaction();
                }
                return QContactManager::UnspecifiedError;
            }
            while (m_findConstituentsForAggregateIds.next()) {
                quint32 dbId = m_findConstituentsForAggregateIds.value(0).toUInt();
                boundAggregatesToRemove.append(dbId); // we just add it to the big list of bound "remove these"
                realRemoveIds.append(ContactId::apiId(dbId));
            }
            m_findConstituentsForAggregateIds.finish();
        }

        // remove the aggregates + the aggregated
        m_removeContact.bindValue(QLatin1String(":contactId"), boundAggregatesToRemove);
        if (!m_removeContact.execBatch()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to removed aggregate contacts (and the contacts they aggregate):\n%1")
                    .arg(m_removeContact.lastError().text()));
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            m_removeContact.finish();
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

    foreach (const QContactId &id, realRemoveIds)
        m_removedIds.insert(id);

    // And notify of any removals.
    if (realRemoveIds.size() > 0) {
        // update our "regenerate list" by purging removed contacts
        foreach (const QContactId &removedId, realRemoveIds) {
            aggregatesOfRemoved.removeAll(ContactId::databaseId(removedId));
        }
    }

    // Now regenerate our remaining aggregates as required.
    if (aggregatesOfRemoved.size() > 0) {
        QContactManager::Error writeError = regenerateAggregates(aggregatesOfRemoved, DetailList(), true);
        if (writeError != QContactManager::NoError)
            return writeError;
    }

    // Success!  If we created a transaction, commit.
    if (!withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after removal"));
        return QContactManager::UnspecifiedError;
    }

    return error;
#endif
}

template<typename T>
QContactDetail::DetailType detailType()
{
    return T::Type;
}

QContactDetail::DetailType detailType(const QContactDetail &detail)
{
    return detail.type();
}

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
    INSERT(rv, QContactDeactivated);
    INSERT(rv, QContactIncidental);
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

template<typename T>
const char *detailTypeName()
{
    return detailTypeName(T::Type);
}

QString detailTypeName(const QContactDetail &detail)
{
    return QString::fromLatin1(detailTypeName(detail.type()));
}

static ContactWriter::DetailList getIdentityDetailTypes()
{
    // The list of types for details that identify a contact
    ContactWriter::DetailList rv;
    rv << detailType<QContactSyncTarget>()
       << detailType<QContactGuid>()
       << detailType<QContactType>();
    return rv;
}

static ContactWriter::DetailList getUnpromotedDetailTypes()
{
    // The list of types for details that are not promoted to an aggregate
    ContactWriter::DetailList rv(getIdentityDetailTypes());
    rv << detailType<QContactDisplayLabel>();
    rv << detailType<QContactGlobalPresence>();
    rv << detailType<QContactStatusFlags>();
    rv << detailType<QContactOriginMetadata>();
    rv << detailType<QContactDeactivated>();
    rv << detailType<QContactIncidental>();
    return rv;
}

static ContactWriter::DetailList getCompositionDetailTypes()
{
    // The list of types for details that are composed to form aggregates
    ContactWriter::DetailList rv;
    rv << detailType<QContactName>();
    rv << detailType<QContactTimestamp>();
    rv << detailType<QContactGender>();
    rv << detailType<QContactFavorite>();
    rv << detailType<QContactBirthday>();
    return rv;
}

static ContactWriter::DetailList getPresenceUpdateDetailTypes()
{
    // The list of types for details whose changes constitute presence updates
    ContactWriter::DetailList rv;
    rv << detailType<QContactPresence>();
    rv << detailType<QContactOriginMetadata>();
    rv << detailType<QContactOnlineAccount>();
    return rv;
}

template<typename T>
static bool detailListContains(const ContactWriter::DetailList &list)
{
    return list.contains(detailType<T>());
}

static bool detailListContains(const ContactWriter::DetailList &list, const QContactDetail &detail)
{
    return list.contains(detailType(detail));
}


template <typename T> bool ContactWriter::removeCommonDetails(
            quint32 contactId, QContactManager::Error *error)
{
    m_removeDetail.bindValue(0, contactId);
    m_removeDetail.bindValue(1, detailTypeName<T>());

    if (!m_removeDetail.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to remove common detail for %1:\n%2").arg(detailTypeName<T>())
                .arg(m_removeDetail.lastError().text()));
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    m_removeDetail.finish();
    return true;
}

template<typename T, typename F>
QVariant detailValue(const T &detail, F field)
{
    return detail.value(field);
}

typedef QMap<int, QVariant> DetailMap;

DetailMap detailValues(const QContactDetail &detail, bool includeProvenance = true)
{
    DetailMap rv(detail.values());

    if (!includeProvenance) {
        DetailMap::iterator it = rv.begin();
        while (it != rv.end()) {
            if (it.key() == QContactDetail__FieldProvenance) {
                it = rv.erase(it);
            } else {
                ++it;
            }
        }
    }

    return rv;
}

static bool variantEqual(const QVariant &lhs, const QVariant &rhs)
{
    // Work around incorrect result from QVariant::operator== when variants contain QList<int>
    static const int QListIntType = QMetaType::type("QList<int>");

    const int lhsType = lhs.userType();
    if (lhsType != rhs.userType()) {
        return false;
    }

    if (lhsType == QListIntType) {
        return (lhs.value<QList<int> >() == rhs.value<QList<int> >());
    }
    return (lhs == rhs);
}

static bool detailValuesEqual(const QContactDetail &lhs, const QContactDetail &rhs)
{
    const DetailMap lhsValues(detailValues(lhs, false));
    const DetailMap rhsValues(detailValues(rhs, false));

    if (lhsValues.count() != rhsValues.count()) {
        return false;
    }

    // Because of map ordering, matching fields should be in the same order in both details
    DetailMap::const_iterator lit = lhsValues.constBegin(), lend = lhsValues.constEnd();
    DetailMap::const_iterator rit = rhsValues.constBegin();
    for ( ; lit != lend; ++lit, ++rit) {
        if (lit.key() != rit.key() || !variantEqual(*lit, *rit)) {
            return false;
        }
    }

    return true;
}

static bool detailValuesSuperset(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // True if all values in rhs are present in lhs
    const DetailMap lhsValues(detailValues(lhs, false));
    const DetailMap rhsValues(detailValues(rhs, false));

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

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
QContactManager::Error ContactWriter::fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                                        QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds)
{
    // Although this is a read operation, it's probably best to make it a transaction
    QMutexLocker locker(ContactsDatabase::accessMutex());

    // Exported IDs are those that the sync adaptor has previously exported, that originate locally
    QSet<quint32> exportedDbIds;
    foreach (const QContactId &id, exportedIds) {
        exportedDbIds.insert(ContactId::databaseId(id));
    }

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while fetching sync contacts"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = syncFetch(syncTarget, lastSync, exportedDbIds, syncContacts, addedContacts, deletedContactIds);
    if (error != QContactManager::NoError) {
        rollbackTransaction();
        return error;
    }

    if (!commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after sync contacts fetch"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::updateSyncContacts(const QString &syncTarget,
                                                         QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                                         const QList<QPair<QContact, QContact> > &remoteChanges)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (conflictPolicy != QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges) {
        // We only support one policy for now
        return QContactManager::NotSupportedError;
    }

    if (remoteChanges.isEmpty())
        return QContactManager::NoError;

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while updating sync contacts"));
        return QContactManager::UnspecifiedError;
    }

    m_suppressedSyncTargets.insert(syncTarget);

    QContactManager::Error error = syncUpdate(syncTarget, conflictPolicy, remoteChanges);
    if (error != QContactManager::NoError) {
        rollbackTransaction();
        return error;
    }

    if (!commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after sync contacts update"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}
#endif

bool ContactWriter::storeOOB(const QString &scope, const QMap<QString, QVariant> &values)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (values.isEmpty())
        return true;

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while storing OOB"));
        return false;
    }

    QStringList valuePairs;
    QVariantList dataValues;
    const QChar colon(QChar::fromLatin1(':'));

    QMap<QString, QVariant>::const_iterator it = values.constBegin(), end = values.constEnd();
    for ( ; it != end; ++it) {
        valuePairs.append(QString::fromLatin1("(?,?)"));
        dataValues.append(scope + colon + it.key());
        dataValues.append(it.value());
    }

    QString statement(QString::fromLatin1("INSERT OR REPLACE INTO OOB (name, value) VALUES %1").arg(valuePairs.join(",")));

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB insert:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
    } else {
        foreach (const QVariant &v, dataValues) {
            query.addBindValue(v);
        }
        if (!query.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert OOB: %1")
                    .arg(query.lastError().text()));
        } else {
            if (!commitTransaction()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after storing OOB"));
                return false;
            }
            return true;
        }
    }

    rollbackTransaction();

    return false;
}

bool ContactWriter::removeOOB(const QString &scope, const QStringList &keys)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing OOB"));
        return false;
    }

    QVariantList keyNames;

    QString statement(QString::fromLatin1("DELETE FROM OOB WHERE name "));

    if (keys.isEmpty()) {
        statement.append(QString::fromLatin1("LIKE '%1%%'").arg(scope));
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB remove:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
    } else {
        foreach (const QVariant &name, keyNames) {
            query.addBindValue(name);
        }

        if (!query.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query OOB: %1")
                    .arg(query.lastError().text()));
        } else {
            if (!commitTransaction()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after removing OOB"));
                return false;
            }
            return true;
        }
    }

    rollbackTransaction();

    return false;
}

QMap<int, QString> contextTypes()
{
    QMap<int, QString> rv;

    rv.insert(QContactDetail::ContextHome, QString::fromLatin1("Home"));
    rv.insert(QContactDetail::ContextWork, QString::fromLatin1("Work"));
    rv.insert(QContactDetail::ContextOther, QString::fromLatin1("Other"));

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

QVariant detailContexts(const QContactDetail &detail)
{
    static const QString separator = QString::fromLatin1(";");

    QStringList contexts;
    foreach (int context, detail.contexts()) {
        contexts.append(contextString(context));
    }
    return QVariant(contexts.join(separator));
}

template <typename T> bool ContactWriter::writeCommonDetails(
            quint32 contactId, const QVariant &detailId, const T &detail, bool syncable, bool wasLocal, QContactManager::Error *error)
{
    const QVariant detailUri = detailValue(detail, QContactDetail::FieldDetailUri);
    const QVariant linkedDetailUris = detailLinkedUris(detail);
    const QVariant contexts = detailContexts(detail);
    const int accessConstraints = static_cast<int>(detail.accessConstraints());
    const QVariant provenance = detailValue(detail, QContactDetail__FieldProvenance);
    const QVariant modifiable = wasLocal ? true : (syncable ? detailValue(detail, QContactDetail__FieldModifiable) : QVariant());

    m_insertDetail.bindValue(0, contactId);
    m_insertDetail.bindValue(1, detailId);
    m_insertDetail.bindValue(2, detailTypeName<T>());
    m_insertDetail.bindValue(3, detailUri);
    m_insertDetail.bindValue(4, linkedDetailUris);
    m_insertDetail.bindValue(5, contexts);
    m_insertDetail.bindValue(6, accessConstraints);
    m_insertDetail.bindValue(7, provenance);
    m_insertDetail.bindValue(8, modifiable);

    if (!m_insertDetail.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to write common details for %1:\n%2\ndetailUri: %3, linkedDetailUris: %4")
                .arg(detailTypeName<T>())
                .arg(m_insertDetail.lastError().text())
                .arg(detailUri.value<QString>())
                .arg(linkedDetailUris.value<QString>()));
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    m_insertDetail.finish();
    return true;
}

// Define the type that another type is generated from
template<typename T>
struct GeneratorType { typedef T type; };
template<>
struct GeneratorType<QContactGlobalPresence> { typedef QContactPresence type; };

template <typename T> bool ContactWriter::writeDetails(
        quint32 contactId,
        QContact *contact,
        QSqlQuery &removeQuery,
        const DetailList &definitionMask,
        const QString &syncTarget,
        bool syncable,
        bool wasLocal,
        QContactManager::Error *error)
{
    if (!definitionMask.isEmpty() &&                                          // only a subset of detail types are being written
        !detailListContains<T>(definitionMask) &&                             // this type is not in the set
        !detailListContains<typename GeneratorType<T>::type>(definitionMask)) // this type's generator type is not in the set
        return true;

    if (!removeCommonDetails<T>(contactId, error))
        return false;

    removeQuery.bindValue(0, contactId);
    if (!removeQuery.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to remove existing details for %1:\n%2").arg(detailTypeName<T>())
                .arg(removeQuery.lastError().text()));
        *error = QContactManager::UnspecifiedError;
        return false;
    }
    removeQuery.finish();

    QList<T> contactDetails(contact->details<T>());
    typename QList<T>::iterator it = contactDetails.begin(), end = contactDetails.end();
    for ( ; it != end; ++it) {
        T &detail(*it);
        QSqlQuery &query = bindDetail(contactId, detail);
        if (!query.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to write details for %1:\n%2").arg(detailTypeName<T>())
                    .arg(query.lastError().text()));
            *error = QContactManager::UnspecifiedError;
            return false;
        }

        QVariant detailId = query.lastInsertId();
        query.finish();

        QString provenance;
        if (syncTarget == aggregateSyncTarget) {
            // Preserve the existing provenance information
            provenance = detail.value(QContactDetail__FieldProvenance).toString();
        } else {
            // This detail is not aggregated from another
            provenance = QString::fromLatin1("%1:%2:%3").arg(contactId).arg(detailId.toUInt()).arg(syncTarget);
        }
        detail.setValue(QContactDetail__FieldProvenance, provenance);
        contact->saveDetail(&detail);

        if (!writeCommonDetails(contactId, detailId, detail, syncable, wasLocal, error)) {
            return false;
        }
    }
    return true;
}

static int presenceOrder(QContactPresence::PresenceState state)
{
#ifdef SORT_PRESENCE_BY_AVAILABILITY
    if (state == QContactPresence::PresenceAvailable) {
        return 0;
    } else if (state == QContactPresence::PresenceAway) {
        return 1;
    } else if (state == QContactPresence::PresenceExtendedAway) {
        return 2;
    } else if (state == QContactPresence::PresenceBusy) {
        return 3;
    } else if (state == QContactPresence::PresenceHidden) {
        return 4;
    } else if (state == QContactPresence::PresenceOffline) {
        return 5;
    }
    return 6;
#else
    return static_cast<int>(state);
#endif
}

static bool betterPresence(const QContactPresence &detail, const QContactPresence &best)
{
    if (best.isEmpty())
        return true;

    QContactPresence::PresenceState detailState(detail.presenceState());
    if (detailState == QContactPresence::PresenceUnknown)
        return false;

    return ((presenceOrder(detailState) < presenceOrder(best.presenceState())) ||
            best.presenceState() == QContactPresence::PresenceUnknown);
}

QContactManager::Error ContactWriter::save(
            QList<QContact> *contacts,
            const DetailList &definitionMask,
            QMap<int, bool> *aggregatesUpdated,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate,
            bool withinSyncUpdate)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    if (contacts->isEmpty())
        return QContactManager::NoError;

    // Check that all of the contacts have the same sync target.
    // Note that empty == "local" for all intents and purposes.
    if (!withinAggregateUpdate && !withinSyncUpdate) {
        QString batchSyncTarget;
        for (int i = 0; i < contacts->count(); ++i) {
            // retrieve current contact's sync target
            QString currSyncTarget = (*contacts)[i].detail<QContactSyncTarget>().syncTarget();
            if (currSyncTarget.isEmpty()) {
                currSyncTarget = localSyncTarget;
            }

            // determine whether it's valid
            if (batchSyncTarget.isEmpty()) {
                batchSyncTarget = currSyncTarget;
            } else if (batchSyncTarget != currSyncTarget) {
                if ((batchSyncTarget == localSyncTarget && currSyncTarget == wasLocalSyncTarget) ||
                    (batchSyncTarget == wasLocalSyncTarget && currSyncTarget == localSyncTarget)) {
                    // "was_local" and "local" are effectively equivalent
                } else {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error: contacts from multiple sync targets specified in single batch save!"));
                    return QContactManager::UnspecifiedError;
                }
            }
        }
    }

    if (!withinTransaction && !beginTransaction()) {
        // only create a transaction if we're not within one already
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while saving contacts"));
        return QContactManager::UnspecifiedError;
    }

    static const DetailList presenceUpdateDetailTypes(getPresenceUpdateDetailTypes());

    bool presenceOnlyUpdate = false;
    if (definitionMask.contains(detailType<QContactPresence>())) {
        // If we only update presence/origin-metadata/online-account, we will report
        // this change as a presence change only
        presenceOnlyUpdate = true;
        foreach (const DetailList::value_type &type, definitionMask) {
            if (!presenceUpdateDetailTypes.contains(type)) {
                presenceOnlyUpdate = false;
                break;
            }
        }
    }

    QContactManager::Error worstError = QContactManager::NoError;
    QContactManager::Error err = QContactManager::NoError;
    for (int i = 0; i < contacts->count(); ++i) {
        QContact &contact = (*contacts)[i];
        QContactId contactId = ContactId::apiId(contact);
        quint32 dbId = ContactId::databaseId(contactId);

        bool aggregateUpdated = false;
        if (dbId == 0) {
            const bool isIncidental(!contact.details<QContactIncidental>().isEmpty());

            // If the contact is incidental, ignore the definitionMask to save all available details
            err = create(&contact, isIncidental ? DetailList() : definitionMask, true, withinAggregateUpdate);
            if (err == QContactManager::NoError) {
                contactId = ContactId::apiId(contact);
                dbId = ContactId::databaseId(contactId);
                m_addedIds.insert(contactId);
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating contact: %1 syncTarget: %2").arg(err).arg(contact.detail<QContactSyncTarget>().syncTarget()));
            }
        } else {
            err = update(&contact, definitionMask, &aggregateUpdated, true, withinAggregateUpdate);
            if (err == QContactManager::NoError) {
                if (presenceOnlyUpdate) {
                    m_presenceChangedIds.insert(contactId);
                } else {
                    m_changedIds.insert(contactId);
                }
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error updating contact %1: %2").arg(ContactId::toString(contactId)).arg(err));
            }
        }
        if (err == QContactManager::NoError) {
            if (aggregatesUpdated) {
                aggregatesUpdated->insert(i, aggregateUpdated);
            }

            QString currSyncTarget(contact.detail<QContactSyncTarget>().syncTarget());
            if (currSyncTarget.isEmpty() || currSyncTarget == localSyncTarget || currSyncTarget == wasLocalSyncTarget) {
                // This contact would cause changes to the partial aggregate for sync target contacts
                m_changedLocalIds.insert(dbId);
            } else if (currSyncTarget != aggregateSyncTarget) {
                if (!m_suppressedSyncTargets.contains(currSyncTarget)) {
                    m_changedSyncTargets.insert(currSyncTarget);
                }
            }
        } else {
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
                const QContactId contactId = ContactId::apiId(contact);
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit contacts"));
            return QContactManager::UnspecifiedError;
        }
    }

    return worstError;
}

template <typename T> void appendDetailType(ContactWriter::DetailList *list)
{
    list->append(T::Type);
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
    appendDetailType<QContactExtendedDetail>(&details);
    appendDetailType<QContactDeactivated>(&details);
    appendDetailType<QContactIncidental>(&details);

    return details;
}

static ContactWriter::DetailList allSingularDetails()
{
    ContactWriter::DetailList details;

    appendDetailType<QContactDisplayLabel>(&details);
    appendDetailType<QContactName>(&details);
    appendDetailType<QContactSyncTarget>(&details);
    appendDetailType<QContactFavorite>(&details);
    appendDetailType<QContactGender>(&details);
    appendDetailType<QContactTimestamp>(&details);
    appendDetailType<QContactBirthday>(&details);
    appendDetailType<QContactOriginMetadata>(&details);
    appendDetailType<QContactStatusFlags>(&details);
    appendDetailType<QContactDeactivated>(&details);
    appendDetailType<QContactIncidental>(&details);

    return details;
}

static QContactManager::Error enforceDetailConstraints(QContact *contact)
{
    static const ContactWriter::DetailList supported(allSupportedDetails());
    static const ContactWriter::DetailList singular(allSingularDetails());

    QHash<ContactWriter::DetailList::value_type, int> detailCounts;

    // look for unsupported detail data.
    foreach (const QContactDetail &det, contact->details()) {
        if (!detailListContains(supported, det)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid detail type: %1 %2").arg(detailTypeName(det)).arg(det.type()));
            return QContactManager::InvalidDetailError;
        } else {
            ++detailCounts[detailType(det)];
        }
    }

    // enforce uniqueness constraints
    foreach (const ContactWriter::DetailList::value_type &type, singular) {
        if (detailCounts[type] > 1) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid count of detail type %1: %2").arg(detailTypeName(type)).arg(detailCounts[type]));
            return QContactManager::LimitReachedError;
        }
    }

    return QContactManager::NoError;
}

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
static void adjustDetailUrisForLocal(QContactDetail &currDet)
{
    // A local detail should not reproduce the detail URI information from another contact's details
    currDet.setDetailUri(QString());
    currDet.setLinkedDetailUris(QStringList());
}

/*
    This function is called when an aggregate contact is updated directly.
    The addDelta and remDelta are calculated from the existing (database) aggregate,
    and then these deltas are applied (within this function) to the local contact.
*/
static void promoteDetailsToLocal(const QList<QContactDetail> addDelta, const QList<QContactDetail> remDelta, QContact *localContact)
{
    // first apply the removals.  Note that these may not always apply
    // (eg, if the client attempted to manually remove a detail which
    // comes from a synced contact, rather than the local contact) -
    // in which case, it'll be ignored.
    QList<QContactDetail> notPresentInLocal;
    foreach (const QContactDetail &det, remDelta) {
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

    foreach (const QContactDetail &original, addDelta) {
        static const ContactWriter::DetailList compositionDetailTypes(getCompositionDetailTypes());

        // handle unique details specifically.
        const QContactDetail::DetailType type(detailType(original));
        if (compositionDetailTypes.contains(type)) {
            if (type == detailType<QContactName>()) {
                QContactName lcn = localContact->detail<QContactName>();
                lcn.setPrefix(original.value<QString>(QContactName::FieldPrefix));
                lcn.setFirstName(original.value<QString>(QContactName::FieldFirstName));
                lcn.setMiddleName(original.value<QString>(QContactName::FieldMiddleName));
                lcn.setLastName(original.value<QString>(QContactName::FieldLastName));
                lcn.setSuffix(original.value<QString>(QContactName::FieldSuffix));
                lcn.setValue(QContactName__FieldCustomLabel, original.value(QContactName__FieldCustomLabel));
                localContact->saveDetail(&lcn);
            } else if (type == detailType<QContactTimestamp>()) {
                QContactTimestamp lts = localContact->detail<QContactTimestamp>();
                lts.setLastModified(original.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp));
                lts.setCreated(original.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp));
                localContact->saveDetail(&lts);
            } else if (type == detailType<QContactGender>()) {
                QContactGender lg = localContact->detail<QContactGender>();
                lg.setGender(static_cast<QContactGender::GenderField>(original.value<int>(QContactGender::FieldGender)));
                localContact->saveDetail(&lg);
            } else if (type == detailType<QContactFavorite>()) {
                QContactFavorite lf = localContact->detail<QContactFavorite>();
                lf.setFavorite(original.value<bool>(QContactFavorite::FieldFavorite));
                localContact->saveDetail(&lf);
            } else if (type == detailType<QContactBirthday>()) {
                QContactBirthday bd = localContact->detail<QContactBirthday>();
                bd.setDateTime(original.value<QDateTime>(QContactBirthday::FieldBirthday));
                localContact->saveDetail(&bd);
            }
        } else {
            // other details can be saved to the local contact (if they don't already exist).
            QContactDetail det(original);
            adjustDetailUrisForLocal(det);

            bool needsPromote = true;

            // Don't promote details already in the local, or those not originally present in the local
            QList<QContactDetail> noPromoteDetails(localContact->details() + notPresentInLocal);
            foreach (const QContactDetail &ld, noPromoteDetails) {
                if (detailsEquivalent(det, ld)) {
                    needsPromote = false;
                    break;
                }
            }

            if (needsPromote) {
                // Remove any provenance in this detail so it attaches to the local
                det.setValue(QContactDetail__FieldProvenance, QString());

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

    QContact first, second;
    first.setId(firstId);
    second.setId(secondId);
    relationship.setFirst(first);
    relationship.setSecond(second);

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
    QList<QContact> writeList;
    QContactManager::Error deltaError = calculateDelta(contact, definitionMask, &addDeltaDetails, &remDeltaDetails, &writeList);
    if (deltaError != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to calculate delta for modified aggregate"));
        return deltaError;
    }

    if (addDeltaDetails.isEmpty() && remDeltaDetails.isEmpty() && writeList.isEmpty()) {
        return QContactManager::NoError; // nothing to do.
    }

    bool createdNewLocal = false;
    QContact localContact;

    if (!addDeltaDetails.isEmpty() || !remDeltaDetails.isEmpty()) {
        m_findLocalForAggregate.bindValue(":aggregateId", ContactId::databaseId(contact->id()));
        if (!m_findLocalForAggregate.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to query local for aggregate during update:\n%1")
                    .arg(m_findLocalForAggregate.lastError().text()));
            return QContactManager::UnspecifiedError;
        }

        if (m_findLocalForAggregate.next()) {
            // found the existing local contact aggregated by this aggregate.
            QList<quint32> whichList;
            whichList.append(m_findLocalForAggregate.value(0).toUInt());

            m_findLocalForAggregate.finish();

            QContactFetchHint hint;
            hint.setOptimizationHints(QContactFetchHint::NoRelationships);

            QList<QContact> readList;
            QContactManager::Error readError = m_reader->readContacts(QLatin1String("UpdateAggregate"), &readList, whichList, hint);
            if (readError != QContactManager::NoError || readList.size() == 0) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to read local contact for aggregate %1 during update")
                        .arg(ContactId::toString(*contact)));
                return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
            }

            localContact = readList.at(0);
        } else {
            m_findLocalForAggregate.finish();

            // no local contact exists for the aggregate.  Create a new one.
            createdNewLocal = true;

            // This is an incidental contact, which exists to store changes to an unmodifiable contact
            QContactIncidental incidental;
            localContact.saveDetail(&incidental);

            QContactSyncTarget lst;
            lst.setSyncTarget(localSyncTarget);
            localContact.saveDetail(&lst);

            // Copy some identifying detail to the local
            QContactName lcn = contact->detail<QContactName>();
            bool copyName = (!lcn.firstName().isEmpty() || !lcn.lastName().isEmpty());
            if (!copyName) {
                // This name fails to adequately identify the contact - copy a nickname instead, if available
                copyName = (!lcn.prefix().isEmpty() || !lcn.middleName().isEmpty() || !lcn.suffix().isEmpty());
                foreach (QContactNickname nick, contact->details<QContactNickname>()) {
                    if (!nick.nickname().isEmpty()) {
                        adjustDetailUrisForLocal(nick);
                        localContact.saveDetail(&nick);

                        // We have found a usable nickname - ignore the name detail
                        copyName = false;
                        break;
                    }
                }
            }
            if (copyName) {
                adjustDetailUrisForLocal(lcn);
                localContact.saveDetail(&lcn);
            }
        }

        // promote delta to local contact
        promoteDetailsToLocal(addDeltaDetails, remDeltaDetails, &localContact);
        writeList.append(localContact);
    }

    // update (or create) the local contact
    QMap<int, bool> aggregatesUpdated;
    QMap<int, QContactManager::Error> errorMap;
    QContactManager::Error writeError = save(&writeList, definitionMask, &aggregatesUpdated, &errorMap, withinTransaction, true, false);
    if (writeError != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to update (or create) local contact for modified aggregate"));
        return writeError;
    }

    if (createdNewLocal) {
        // Add the aggregates relationship
        QList<QContactRelationship> saveRelationshipList;
        saveRelationshipList.append(makeRelationship(relationshipString(QContactRelationship::Aggregates), contact->id(), writeList.at(0).id()));
        writeError = save(saveRelationshipList, &errorMap, withinTransaction);
        if (writeError != QContactManager::NoError) {
            // if the aggregation relationship fails, the entire save has failed.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save aggregation relationship for new local contact!"));
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
        writeError = save(&writeList, definitionMask, 0, &errorMap, withinTransaction, true, false); // we're updating the aggregate contact deliberately.
        if (writeError != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to update modified aggregate"));
            if (createdNewLocal) {
                QList<QContactId> removeList;
                removeList.append(ContactId::apiId(localContact));
                QContactManager::Error removeError = remove(removeList, &errorMap, withinTransaction);
                if (removeError != QContactManager::NoError) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove stale local contact created for modified aggregate"));
                }
            }
        }
    }

    return writeError;
}

static void adjustDetailUrisForAggregate(QContactDetail &currDet, quint32 aggId)
{
    static const QString prefixFormat(aggregateSyncTarget + QString::fromLatin1("-%1:"));
    const QString prefix(prefixFormat.arg(aggId));

    if (!currDet.detailUri().isEmpty()) {
        currDet.setDetailUri(prefix + currDet.detailUri());
    }

    bool needsLinkedDUs = false;
    QStringList linkedDUs = currDet.linkedDetailUris();
    for (int i = 0; i < linkedDUs.size(); ++i) {
        QString currLDU = linkedDUs.at(i);
        if (!currLDU.isEmpty()) {
            currLDU = prefix + currLDU;
            linkedDUs.replace(i, currLDU);
            needsLinkedDUs = true;
        }
    }
    if (needsLinkedDUs) {
        currDet.setLinkedDetailUris(linkedDUs);
    }
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

    const quint32 aggId = ContactId::databaseId(*aggregate);

    foreach (const QContactDetail &original, contact.details()) {
        if (unpromotedDetailTypes.contains(detailType(original))) {
            // don't promote this detail.
            continue;
        }
        if (!definitionMask.isEmpty() && !detailListContains(definitionMask, original)) {
            // skip this detail
            continue;
        }

        // promote this detail to the aggregate.  Depending on uniqueness,
        // this consists either of composition or duplication.
        // Note: Composed (unique) details won't have any detailUri!
        if (detailType(original) == detailType<QContactName>()) {
            // name involves composition
            QContactName cname(original);
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
            QString customLabel = cname.value<QString>(QContactName__FieldCustomLabel);
            if (!customLabel.isEmpty() && aname.value<QString>(QContactName__FieldCustomLabel).isEmpty())
                aname.setValue(QContactName__FieldCustomLabel, cname.value(QContactName__FieldCustomLabel));
            aggregate->saveDetail(&aname);
        } else if (detailType(original) == detailType<QContactTimestamp>()) {
            // timestamp involves composition; an incidental local contact must be given the
            // created timestamp from the aggregate from which it is derived to preserve the aggregate timestamp
            // Note: From some sync sources, the creation timestamp will precede the existence of the local device.
            QContactTimestamp cts(original);
            QContactTimestamp ats(aggregate->detail<QContactTimestamp>());
            if (cts.lastModified().isValid() && (!ats.lastModified().isValid() || cts.lastModified() > ats.lastModified())) {
                ats.setLastModified(cts.lastModified());
                aggregate->saveDetail(&ats);
            }
        } else if (detailType(original) == detailType<QContactGender>()) {
            // gender involves composition
            QContactGender cg(original);
            QContactGender ag(aggregate->detail<QContactGender>());
            // In Qtpim, uninitialized gender() does not default to GenderUnspecified...
            if (cg.gender() != QContactGender::GenderUnspecified &&
                (ag.gender() != QContactGender::GenderMale && ag.gender() != QContactGender::GenderFemale)) {
                ag.setGender(cg.gender());
                aggregate->saveDetail(&ag);
            }
        } else if (detailType(original) == detailType<QContactFavorite>()) {
            // favorite involves composition
            QContactFavorite cf(original);
            QContactFavorite af(aggregate->detail<QContactFavorite>());
            if (cf.isFavorite() && !af.isFavorite()) {
                af.setFavorite(true);
                aggregate->saveDetail(&af);
            }
        } else if (detailType(original) == detailType<QContactBirthday>()) {
            // birthday involves composition (at least, it's unique)
            QContactBirthday cb(original);
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
            QContactDetail det(original);
            adjustDetailUrisForAggregate(det, aggId);

            bool needsPromote = true;
            foreach (const QContactDetail &ad, aggregate->details()) {
                if (detailsEquivalent(det, ad)) {
                    needsPromote = false;
                    break;
                }
            }

            if (needsPromote) {
                QString syncTarget(contact.detail<QContactSyncTarget>().value<QString>(QContactSyncTarget::FieldSyncTarget));
                if (!syncTarget.isEmpty() &&
                    syncTarget != localSyncTarget &&
                    syncTarget != wasLocalSyncTarget &&
                    (original.value<bool>(QContactDetail__FieldModifiable) != true)) {
                    QContactManagerEngine::setDetailAccessConstraints(&det, QContactDetail::ReadOnly | QContactDetail::Irremovable);
                }

                // Store the provenance of this promoted detail
                det.setValue(QContactDetail__FieldProvenance, original.value<QString>(QContactDetail__FieldProvenance));

                aggregate->saveDetail(&det);
            }
        }
    }
}

typedef QPair<QString, QString> StringPair;
typedef QPair<QContactDetail, QContactDetail> DetailPair;

static QList<QPair<QContactDetail, StringPair> > contactDetails(const QContact &contact, const ContactWriter::DetailList &definitionMask = ContactWriter::DetailList())
{
    static const ContactWriter::DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());

    QList<QPair<QContactDetail, StringPair> > rv;

    foreach (const QContactDetail &original, contact.details()) {
        if (!unpromotedDetailTypes.contains(detailType(original)) &&
            (definitionMask.isEmpty() || detailListContains(definitionMask, original))) {
            // Make mutable copies of the contacts' detail (without Irremovable|Readonly flags)
            QContactDetail copy(original);
            QContactManagerEngine::setDetailAccessConstraints(&copy, QContactDetail::NoConstraint);

            const QString provenance(original.value<QString>(QContactDetail__FieldProvenance));
            const StringPair identity(qMakePair(provenance, detailTypeName(original)));
            rv.append(qMakePair(copy, identity));
        }
    }

    return rv;
}

static void removeEquivalentDetails(QList<QPair<QContactDetail, StringPair> > &original, QList<QPair<QContactDetail, StringPair> > &updated)
{
    // Determine which details are in the update contact which aren't in the database contact:
    // Detail order is not defined, so loop over the entire set for each, removing matches or
    // superset details (eg, backend added a field (like lastModified to timestamp) on previous save)
    QList<QPair<QContactDetail, StringPair> >::iterator oit = original.begin(), oend;
    while (oit != original.end()) {
        QList<QPair<QContactDetail, StringPair> >::iterator uit = updated.begin(), uend = updated.end();
        for ( ; uit != uend; ++uit) {
            if (detailsSuperset((*oit).first, (*uit).first)) {
                // These details match - remove from the lists
                updated.erase(uit);
                break;
            }
        }
        if (uit != uend) {
            // We found a match
            oit = original.erase(oit);
        } else {
            ++oit;
        }
    }
}

static QContactDetail findContactDetail(QContact *contact, const StringPair &identity)
{
    foreach (const QContactDetail &detail, contact->details()) {
        if (detailTypeName(detail) == identity.second) {
            const QString provenance(detail.value(QContactDetail__FieldProvenance).toString());
            if (provenance == identity.first) {
                return detail;
            }
        }
    }

    return QContactDetail();
}

static bool removeContactDetails(QContact *contact, const QList<StringPair> &removals)
{
    foreach (const StringPair &identity, removals) {
        QContactDetail detail(findContactDetail(contact, identity));
        if (!contact->removeDetail(&detail)) {
            return false;
        }
    }

    return true;
}

static bool modifyContactDetails(QContact *contact, const QList<QPair<StringPair, QContactDetail> > &updates)
{
    QList<QPair<StringPair, QContactDetail> >::const_iterator it = updates.constBegin(), end = updates.constEnd();
    for ( ; it != end; ++it) {
        QContactDetail detail(findContactDetail(contact, (*it).first));
        if (!contact->removeDetail(&detail)) {
            return false;
        }

        // Ensure this detail is marked as modifiable, since it must originally have been to get to this point
        QContactDetail updated((*it).second);
        updated.setValue(QContactDetail__FieldModifiable, true);
        if (!contact->saveDetail(&updated)) {
            return false;
        }
    }

    return true;
}

/*
    This function is called as part of the "save updated aggregate"
    codepath.  It calculates the list of details which were modified
    in the updated version (compared to the current database version)
    and returns it.
*/
QContactManager::Error ContactWriter::calculateDelta(QContact *contact, const ContactWriter::DetailList &definitionMask,
                                                     QList<QContactDetail> *addDelta, QList<QContactDetail> *removeDelta, QList<QContact> *writeList)
{
    static const ContactWriter::DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());

    QContactFetchHint hint;
    hint.setDetailTypesHint(definitionMask);
    hint.setOptimizationHints(QContactFetchHint::NoRelationships);

    QList<quint32> whichList;
    whichList.append(ContactId::databaseId(*contact));

    // Load the existing state of the aggregate from DB
    QList<QContact> readList;
    QContactManager::Error readError = m_reader->readContacts(QLatin1String("CalculateDelta"), &readList, whichList, hint);
    if (readError != QContactManager::NoError || readList.size() == 0) {
        // unable to read the aggregate contact from the database
        return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
    }

    QList<QPair<QContactDetail, StringPair> > originalDetails(contactDetails(readList.at(0)));
    QList<QPair<QContactDetail, StringPair> > updateDetails(contactDetails(*contact));

    removeEquivalentDetails(originalDetails, updateDetails);

    QMap<quint32, QList<QPair<StringPair, QContactDetail> > > contactModifications;
    QMap<quint32, QList<StringPair> > contactRemovals;

    if (!originalDetails.isEmpty()) {
        QSet<quint32> originContactIds;

        // Get the set of IDs for contacts from which the aggregate's remaining details are promoted
        QList<QPair<QContactDetail, StringPair> >::iterator oit = originalDetails.begin(), oend = originalDetails.end();
        for ( ; oit != oend; ++oit) {
            const StringPair &identity((*oit).second);
            if (!identity.first.isEmpty()) {
                const QStringList provenance(identity.first.split(QChar::fromLatin1(':')));
                originContactIds.insert(provenance.at(0).toUInt());
            }
        }

        QVariantList ids;
        foreach (quint32 id, originContactIds) {
            ids.append(id);
        }

        ContactsDatabase::clearTemporaryContactIdsTable(m_database, modifiableContactsTable);

        if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, modifiableContactsTable, ids)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating temporary table %1").arg(modifiableContactsTable));
            return QContactManager::UnspecifiedError;
        }

        // See if any of these details are modifiable in-place
        if (!m_modifiableDetails.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to select modifiable details:\n%2")
                    .arg(m_modifiableDetails.lastError().text()));
        } else {
            while (m_modifiableDetails.next()) {
                const QString provenance = m_modifiableDetails.value(0).toString();
                const QString detail = m_modifiableDetails.value(1).toString();
                const quint32 contactId = m_modifiableDetails.value(2).toUInt();

                const StringPair identity = qMakePair(provenance, detail);

                for (oit = originalDetails.begin(), oend = originalDetails.end(); oit != oend; ++oit) {
                    if ((*oit).second == identity) {
                        // We have a modifiable detail to modify - find the updated version of this detail, if present
                        QList<QPair<QContactDetail, StringPair> >::iterator uit = updateDetails.begin(), uend = updateDetails.end();
                        for ( ; uit != uend; ++uit) {
                            if ((*uit).second == identity) {
                                // Found a match - modify the original detail in the original contact
                                contactModifications[contactId].append(qMakePair(identity, (*uit).first));
                                updateDetails.erase(uit);
                                break;
                            }
                        }
                        if (uit == uend) {
                            // No matching update - remove the detail from the original contact
                            contactRemovals[contactId].append(identity);
                        }

                        // No longer need to make this change to the local
                        originalDetails.erase(oit);
                        break;
                    }
                }
            }
        }
        m_modifiableDetails.finish();
    }

    if (!contactModifications.isEmpty() || !contactRemovals.isEmpty()) {
        QSet<quint32> retrievalIds;
        foreach (quint32 contactId, contactModifications.keys() + contactRemovals.keys()) {
            retrievalIds.insert(contactId);
        }
        whichList = retrievalIds.toList();

        QContactManager::Error readError = m_reader->readContacts(QLatin1String("CalculateDelta"), writeList, whichList, hint);
        if (readError != QContactManager::NoError || writeList->size() != whichList.size()) {
            // unable to read the affected contacts from the database
            return readError == QContactManager::NoError ? QContactManager::UnspecifiedError : readError;
        }

        QMap<quint32, QContact *> updatedContacts;
        QList<QContact>::iterator it = writeList->begin(), end = writeList->end();
        for ( ; it != end; ++it) {
            updatedContacts.insert(ContactId::databaseId((*it).id()), &*it);
        }

        // Delete any removed details from the relevant contact
        QMap<quint32, QList<StringPair> >::const_iterator rit = contactRemovals.constBegin(), rend = contactRemovals.constEnd();
        for ( ; rit != rend; ++rit) {
            QMap<quint32, QContact *>::iterator cit = updatedContacts.find(rit.key());
            if (cit != updatedContacts.end()) {
                if (!removeContactDetails(cit.value(), rit.value())) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove details from constituent contact: %1").arg(cit.key()));
                    return QContactManager::UnspecifiedError;
                }
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to find constituent contact to remove detail: %1").arg(rit.key()));
                return QContactManager::UnspecifiedError;
            }
        }

        // Update modified details from the relevant contact
        QMap<quint32, QList<QPair<StringPair, QContactDetail> > >::const_iterator mit = contactModifications.constBegin(), mend = contactModifications.constEnd();
        for ( ; mit != mend; ++mit) {
            QMap<quint32, QContact *>::iterator cit = updatedContacts.find(mit.key());
            if (cit != updatedContacts.end()) {
                if (!modifyContactDetails(cit.value(), mit.value())) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove details from constituent contact: %1").arg(cit.key()));
                    return QContactManager::UnspecifiedError;
                }
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to find constituent contact to remove detail: %1").arg(mit.key()));
                return QContactManager::UnspecifiedError;
            }
        }
    }

    // These are details which exist in the database contact, but not in the updated contact.
    QList<QPair<QContactDetail, StringPair> >::const_iterator oit = originalDetails.constBegin(), oend = originalDetails.constEnd();
    for (; oit != oend; ++oit) {
        removeDelta->append((*oit).first);
    }

    // These are details which exist in the updated contact, but not in the database contact.
    QList<QPair<QContactDetail, StringPair> >::const_iterator uit = updateDetails.constBegin(), uend = updateDetails.constEnd();
    for ( ; uit != uend; ++uit) {
        addDelta->append((*uit).first);
    }

    return QContactManager::NoError;
}

/*
   This function is called when a new contact is created.  The
   aggregate contacts are searched for a match, and the matching
   one updated if it exists; or a new aggregate is created.
*/
QContactManager::Error ContactWriter::updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction, quint32 *aggregateContactId)
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
    QVariantList phoneNumbers;
    QVariantList emailAddresses;
    QVariantList accountUris;
    QString syncTarget;
    QString excludeGender;

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
        syncTarget = localSyncTarget;
    }

    const QContactGender gender(contact->detail<QContactGender>());
    const QString gv(gender.gender() == QContactGender::GenderFemale ? QString::fromLatin1("Female") :
                     gender.gender() == QContactGender::GenderMale ? QString::fromLatin1("Male") : QString());
    if (gv == QString::fromLatin1("Male")) {
        excludeGender = QString::fromLatin1("Female");
    } else if (gv == QString::fromLatin1("Female")) {
        excludeGender = QString::fromLatin1("Male");
    } else {
        excludeGender = QString::fromLatin1("none");
    }

    // Use a simple match algorithm, looking for exact matches on name fields,
    // or accumulating points for name matches (including partial matches of first name).
    QContact matchingAggregate;
    bool found = false;

    // step one: build the temporary table which contains all "possible" aggregate contact ids.
    ContactsDatabase::clearTemporaryContactIdsTable(m_database, possibleAggregatesTable);

    QString orderBy = QLatin1String("contactId ASC ");
    QString where = QLatin1String(possibleAggregatesWhere);
    QMap<QString, QVariant> bindings;
    bindings.insert(":lastName", lastName);
    bindings.insert(":contactId", contactId);
    bindings.insert(":excludeGender", excludeGender);
    if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, possibleAggregatesTable,
                                                          QString(), where, orderBy, bindings)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating possibleAggregates temporary table"));
        return QContactManager::UnspecifiedError;
    }

    // step two: query matching data.
    m_heuristicallyMatchData.bindValue(":firstName", firstName);
    m_heuristicallyMatchData.bindValue(":lastName", lastName);
    m_heuristicallyMatchData.bindValue(":nickname", nickname);

    ContactsDatabase::clearTemporaryValuesTable(m_database, matchEmailAddressesTable);
    ContactsDatabase::clearTemporaryValuesTable(m_database, matchPhoneNumbersTable);
    ContactsDatabase::clearTemporaryValuesTable(m_database, matchOnlineAccountsTable);

    if (!ContactsDatabase::createTemporaryValuesTable(m_database, matchEmailAddressesTable, emailAddresses) ||
        !ContactsDatabase::createTemporaryValuesTable(m_database, matchPhoneNumbersTable, phoneNumbers) ||
        !ContactsDatabase::createTemporaryValuesTable(m_database, matchOnlineAccountsTable, accountUris)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating possibleAggregates match tables"));
        return QContactManager::UnspecifiedError;
    }

    if (!m_heuristicallyMatchData.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error finding match for updated local contact:\n%1")
                .arg(m_heuristicallyMatchData.lastError().text()));
        return QContactManager::UnspecifiedError;
    }
    if (m_heuristicallyMatchData.next()) {
        const quint32 aggregateId = m_heuristicallyMatchData.value(0).toUInt();
        const quint32 score = m_heuristicallyMatchData.value(1).toUInt();
        m_heuristicallyMatchData.finish();

        static const quint32 MinimumMatchScore = 15;
        if (score >= MinimumMatchScore) {
            QList<quint32> readIds;
            readIds.append(aggregateId);

            QContactFetchHint hint;
            hint.setOptimizationHints(QContactFetchHint::NoRelationships);

            QList<QContact> readList;
            QContactManager::Error readError = m_reader->readContacts(QLatin1String("CreateAggregate"), &readList, readIds, hint);
            if (readError != QContactManager::NoError || readList.size() < 1) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read aggregate contact %1 during regenerate").arg(aggregateId));
                return QContactManager::UnspecifiedError;
            }

            matchingAggregate = readList.at(0);
            found = true;
        }
    }
    m_heuristicallyMatchData.finish();

    // whether it's an existing or new contact, we promote details.
    // TODO: promote non-Aggregates relationships!
    promoteDetailsToAggregate(*contact, &matchingAggregate, definitionMask);
    if (!found) {
        // need to create an aggregating contact first.
        QContactSyncTarget cst;
        cst.setSyncTarget(aggregateSyncTarget);
        matchingAggregate.saveDetail(&cst);
    }

    // now save in database.
    saveContactList.append(matchingAggregate);
    QContactManager::Error err = save(&saveContactList, DetailList(), 0, &errorMap, withinTransaction, true, false); // we're updating (or creating) the aggregate
    if (err != QContactManager::NoError) {
        if (!found) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not create new aggregate contact"));
        } else {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not update existing aggregate contact"));
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error inserting Aggregates relationship: %1").arg(m_insertRelationship.lastError().text()));
        err = QContactManager::UnspecifiedError;
    }

    if (err == QContactManager::NoError) {
        if (aggregateContactId) {
            *aggregateContactId = ContactId::databaseId(matchingAggregate);
        }
    } else {
        // if the aggregation relationship fails, the entire save has failed.
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save aggregation relationship!"));

        if (!found) {
            // clean up the newly created contact.
            QList<QContactId> removeList;
            removeList.append(ContactId::apiId(matchingAggregate));
            QContactManager::Error cleanupErr = remove(removeList, &errorMap, withinTransaction);
            if (cleanupErr != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to cleanup newly created aggregate contact!"));
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
QContactManager::Error ContactWriter::regenerateAggregates(const QList<quint32> &aggregateIds, const DetailList &definitionMask, bool withinTransaction)
{
    static const DetailList identityDetailTypes(getIdentityDetailTypes());
    static const DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());

    // for each aggregate contact:
    // 1) get the contacts it aggregates
    // 2) build unique details via composition (name / timestamp / gender / favorite - NOT synctarget or guid)
    // 3) append non-unique details
    // In all cases, we "prefer" the 'local' contact's data (if it exists)

    QList<QContact> aggregatesToSave;
    QSet<QContactId> aggregatesToSaveIds;
    QVariantList aggregatesToRemove;

    foreach (quint32 aggId, aggregateIds) {
        QContactId apiId(ContactId::apiId(aggId));
        if (aggregatesToSaveIds.contains(apiId)) {
            continue;
        }

        QList<quint32> readIds;
        readIds.append(aggId);

        m_findConstituentsForAggregate.bindValue(":aggregateId", aggId);
        if (!m_findConstituentsForAggregate.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to find constituent contacts for aggregate %1 during regenerate").arg(aggId));
            return QContactManager::UnspecifiedError;
        }
        while (m_findConstituentsForAggregate.next()) {
            readIds.append(m_findConstituentsForAggregate.value(0).toUInt());
        }
        m_findConstituentsForAggregate.finish();

        if (readIds.size() == 1) { // only the aggregate?
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Existing aggregate %1 should already have been removed - aborting regenerate").arg(aggId));
            return QContactManager::UnspecifiedError;
        }

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("RegenerateAggregate"), &readList, readIds, hint);
        if (readError != QContactManager::NoError
                || readList.size() <= 1
                || readList.at(0).detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != aggregateSyncTarget) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read constituent contacts for aggregate %1 during regenerate").arg(aggId));
            return QContactManager::UnspecifiedError;
        }

        // See if there are any constituents to aggregate
        bool activeConstituent = false;
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            const QContact &curr(readList.at(i));
            if (curr.details<QContactDeactivated>().count() == 0) {
                activeConstituent = true;
                break;
            }
        }
        if (!activeConstituent) {
            // No active constituents - we need to remove this aggregate
            aggregatesToRemove.append(QVariant(aggId));
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
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Contact: %1 Failed to copy existing detail:")
                            .arg(ContactId::toString(aggregateContact)) << detail);
                }
            }
        }

        // Step two: search for the "local" contact and promote its details first
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.details<QContactDeactivated>().count())
                continue;
            if (curr.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != localSyncTarget)
                continue;
            QList<QContactDetail> currDetails = curr.details();
            for (int j = 0; j < currDetails.size(); ++j) {
                QContactDetail currDet = currDetails.at(j);
                if (!detailListContains(unpromotedDetailTypes, currDet) &&
                    (definitionMask.isEmpty() || detailListContains(definitionMask, currDet))) {
                    // promote this detail to the aggregate.
                    adjustDetailUrisForAggregate(currDet, aggId);
                    aggregateContact.saveDetail(&currDet);
                }
            }
            break; // we've successfully promoted the local contact's details to the aggregate.
        }

        // Step Three: promote data from details of other related contacts
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.details<QContactDeactivated>().count())
                continue;
            if (curr.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) == localSyncTarget) {
                continue; // already promoted the "local" contact's details.
            }

            // need to promote this contact's details to the aggregate
            promoteDetailsToAggregate(curr, &aggregateContact, definitionMask);
        }

        // we save the updated aggregates to database all in a batch at the end.
        aggregatesToSave.append(aggregateContact);
        aggregatesToSaveIds.insert(ContactId::apiId(aggregateContact));
    }

    if (!aggregatesToSave.isEmpty()) {
        QMap<int, QContactManager::Error> errorMap;
        QContactManager::Error writeError = save(&aggregatesToSave, definitionMask, 0, &errorMap, withinTransaction, true, false); // we're updating aggregates.
        if (writeError != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to write updated aggregate contacts during regenerate.  definitionMask:") << definitionMask);
            return writeError;
        }
    }
    if (!aggregatesToRemove.isEmpty()) {
        m_removeContact.bindValue(QLatin1String(":contactId"), aggregatesToRemove);
        if (!m_removeContact.execBatch()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to remove deactivated aggregate contacts:\n%1")
                    .arg(m_removeContact.lastError().text()));
            m_removeContact.finish();
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.finish();
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::removeChildlessAggregates(QList<QContactId> *removedIds)
{
    QVariantList aggregateIds;
    if (!m_childlessAggregateIds.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch childless aggregate contact ids during remove:\n%1")
                .arg(m_childlessAggregateIds.lastError().text()));
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
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to remove childless aggregate contacts:\n%1")
                    .arg(m_removeContact.lastError().text()));
            m_removeContact.finish();
            return QContactManager::UnspecifiedError;
        }
        m_removeContact.finish();
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::aggregateOrphanedContacts(bool withinTransaction)
{
    QList<quint32> contactIds;
    if (!m_orphanContactIds.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch orphan aggregate contact ids during remove:\n%1")
                .arg(m_orphanContactIds.lastError().text()));
        return QContactManager::UnspecifiedError;
    }
    while (m_orphanContactIds.next()) {
        contactIds.append(m_orphanContactIds.value(0).toUInt());
    }
    m_orphanContactIds.finish();

    if (contactIds.size() > 0) {
        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("AggregateOrphaned"), &readList, contactIds, hint);
        if (readError != QContactManager::NoError || readList.size() != contactIds.size()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read orphaned contacts for aggregation"));
            return QContactManager::UnspecifiedError;
        }

        QList<QContact>::iterator it = readList.begin(), end = readList.end();
        for ( ; it != end; ++it) {
            QContact &orphan(*it);
            QContactManager::Error error = updateOrCreateAggregate(&orphan, DetailList(), withinTransaction);
            if (error != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create aggregate for orphaned contact: %1").arg(ContactId::toString(orphan)));
                return error;
            }
        }
    }

    return QContactManager::NoError;
}

static QDateTime epochDateTime()
{
    QDateTime rv;
    rv.setMSecsSinceEpoch(0);
    return rv;
}

QContactManager::Error ContactWriter::syncFetch(const QString &syncTarget, const QDateTime &lastSync, const QSet<quint32> &exportedIds,
                                                QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds)
{
    static const DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());

    const QDateTime since(lastSync.isValid() ? lastSync : epochDateTime());

    if (syncContacts || addedContacts) {
        QSet<quint32> aggregateIds;
        QSet<quint32> addedAggregateIds;

        if (syncContacts) {
            // Find all aggregates of contacts from this sync service modified since the last sync
            m_syncContactIds.bindValue(":syncTarget", syncTarget);
            m_syncContactIds.bindValue(":lastSync", since);
            if (!m_syncContactIds.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch sync contact ids:\n%1")
                        .arg(m_syncContactIds.lastError().text()));
                return QContactManager::UnspecifiedError;
            }
            while (m_syncContactIds.next()) {
                aggregateIds.insert(m_syncContactIds.value(0).toUInt());
            }
            m_syncContactIds.finish();

            if (!exportedIds.isEmpty()) {
                // Add the previously-exported contact IDs
                QVariantList ids;
                foreach (quint32 id, exportedIds) {
                    ids.append(id);
                }

                ContactsDatabase::clearTemporaryContactIdsTable(m_database, syncConstituentsTable);

                if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, syncConstituentsTable, ids)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncConstituents temporary table"));
                    return QContactManager::UnspecifiedError;
                }

                m_aggregateContactIds.bindValue(":lastSync", since);
                if (!m_aggregateContactIds.exec()) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch aggregate contact ids for sync:\n%1")
                            .arg(m_aggregateContactIds.lastError().text()));
                    return QContactManager::UnspecifiedError;
                }
                while (m_aggregateContactIds.next()) {
                    aggregateIds.insert(m_aggregateContactIds.value(0).toUInt());
                }
                m_aggregateContactIds.finish();
            }
        }

        if (addedContacts) {
            // Report added contacts as well
            m_addedSyncContactIds.bindValue(":lastSync", since);
            if (!m_addedSyncContactIds.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch sync contact ids:\n%1")
                        .arg(m_addedSyncContactIds.lastError().text()));
                return QContactManager::UnspecifiedError;
            }
            while (m_addedSyncContactIds.next()) {
                const quint32 aggregateId(m_addedSyncContactIds.value(0).toUInt());

                // Fetch the aggregates for the added contacts, unless they're constituents of aggregates we're returning anyway
                if (!aggregateIds.contains(aggregateId)) {
                    aggregateIds.insert(aggregateId);
                    addedAggregateIds.insert(aggregateId);
                }
            }
            m_addedSyncContactIds.finish();
        }

        if (aggregateIds.count() > 0) {
            // Return 'partial aggregates' for each of these contacts - any sync adaptor should see
            // the parts of the aggregate that are sourced from the local device or from their own
            // remote data.  Data from other sync adaptors should be excluded.

            // First, find the constituent details for the aggregates
            QMap<quint32, QList<QPair<quint32, QString> > > constituentDetails;
            QMap<quint32, quint32> localIds;

            QVariantList ids;
            foreach (quint32 id, aggregateIds) {
                ids.append(id);
            }

            ContactsDatabase::clearTemporaryContactIdsTable(m_database, syncAggregatesTable);

            if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, syncAggregatesTable, ids)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncAggregates temporary table"));
                return QContactManager::UnspecifiedError;
            }

            if (!m_constituentContactDetails.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch constituent contact details:\n%1")
                        .arg(m_constituentContactDetails.lastError().text()));
                return QContactManager::UnspecifiedError;
            }
            while (m_constituentContactDetails.next()) {
                const quint32 aggId(m_constituentContactDetails.value(0).toUInt());
                const quint32 constituentId(m_constituentContactDetails.value(1).toUInt());
                const QString st(m_constituentContactDetails.value(2).toString());

                constituentDetails[aggId].append(qMakePair(constituentId, st));
                if (st == localSyncTarget) {
                    localIds[aggId] = constituentId;
                }
            }
            m_constituentContactDetails.finish();

            QMap<quint32, quint32> completeAggregateIds;
            QMap<quint32, quint32> partialBaseAggregateIds;
            QSet<quint32> requiredConstituentIds;

            // For each aggregate - can we return the existing complete aggregate or do we need to generate a limited version?
            QMap<quint32, QList<QPair<quint32, QString> > >::const_iterator it = constituentDetails.constBegin(), end = constituentDetails.constEnd();
            for ( ; it != end; ++it) {
                bool regenerate = false;
                quint32 syncTargetConstituentId = 0;
                QList<quint32> inclusions;

                const quint32 aggId(it.key());
                const QList<QPair<quint32, QString> > &constituents(it.value());

                QList<QPair<quint32, QString> >::const_iterator cit = constituents.constBegin(), cend = constituents.constEnd();
                for ( ; cit != cend; ++cit) {
                    const quint32 cid((*cit).first);
                    const QString cst((*cit).second);

                    if (cst == syncTarget || cst == localSyncTarget || cst == wasLocalSyncTarget) {
                        inclusions.append(cid);
                        if (cst == syncTarget) {
                            if (syncTargetConstituentId != 0) {
                                // We need to generate partial aggregates for each syncTarget constituent
                                regenerate = true;
                                partialBaseAggregateIds.insert(cid, aggId);
                            } else {
                                syncTargetConstituentId = cid;
                            }
                        }
                    } else {
                        // Some elements of the complete aggregate must be excluded
                        regenerate = true;
                    }
                }

                quint32 baseId = 0;
                if (syncTargetConstituentId == 0) {
                    // This aggregate has no syncTarget constituent - base it on the local
                    baseId = localIds[aggId];
                } else {
                    baseId = syncTargetConstituentId;
                }

                if (regenerate) {
                    partialBaseAggregateIds.insert(baseId, aggId);
                    foreach (quint32 id, inclusions) {
                        requiredConstituentIds.insert(id);
                    }
                } else {
                    completeAggregateIds.insert(aggId, baseId);
                }
            }

            // Fetch all the contacts we need - either aggregates to return, or constituents to build partial aggregates from
            QList<quint32> readIds(completeAggregateIds.keys() + requiredConstituentIds.toList());
            if (!readIds.isEmpty()) {
                QContactFetchHint hint;
                hint.setOptimizationHints(QContactFetchHint::NoRelationships);

                QList<QContact> readList;
                QContactManager::Error readError = m_reader->readContacts(QLatin1String("syncFetch"), &readList, readIds, hint, true);
                if (readError != QContactManager::NoError || readList.size() != readIds.size()) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read contacts for sync"));
                    return QContactManager::UnspecifiedError;
                }

                QMap<quint32, const QContact *> constituentContacts;

                foreach (const QContact &contact, readList) {
                    const quint32 dbId(ContactId::databaseId(contact.id()));

                    QMap<quint32, quint32>::const_iterator ait = completeAggregateIds.find(dbId);
                    if (ait != completeAggregateIds.end()) {
                        const quint32 baseId(*ait);

                        // Return this aggregate contact, with the ID set to the base from which it is derived
                        QContact aggregate(contact);
                        aggregate.setId(ContactId::apiId(baseId));

                        if (addedAggregateIds.contains(dbId)) {
                            addedContacts->append(aggregate);
                        } else {
                            syncContacts->append(aggregate);
                        }
                    } else {
                        // We need this contact to build the partial aggregates
                        constituentContacts.insert(dbId, &contact);
                    }
                }

                // Build partial aggregates - keep in sync with related logic in regenerateAggregates
                QMap<quint32, quint32>::const_iterator pit = partialBaseAggregateIds.constBegin(), pend = partialBaseAggregateIds.constEnd();
                for ( ; pit != pend; ++pit) {
                    const quint32 baseId(pit.key());
                    const quint32 aggId(pit.value());

                    QContact partialAggregate;
                    partialAggregate.setId(ContactId::apiId(baseId));

                    // If this aggregate has a local constituent, copy that contact's details first
                    const quint32 localId = localIds[aggId];
                    if (localId) {
                        if (const QContact *localConstituent = constituentContacts[localId]) {
                            foreach (const QContactDetail &detail, localConstituent->details()) {
                                if (!detailListContains(unpromotedDetailTypes, detail)) {
                                    // promote this detail to the aggregate.
                                    QContactDetail copy(detail);
                                    adjustDetailUrisForAggregate(copy, aggId);
                                    partialAggregate.saveDetail(&copy);
                                }
                            }
                        } else {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to promote details from missing local: %1").arg(localId));
                            return QContactManager::UnspecifiedError;
                        }
                    }

                    // Promote details from other constituents
                    const QList<QPair<quint32, QString> > &constituents(constituentDetails[aggId]);
                    QList<QPair<quint32, QString> >::const_iterator cit = constituents.constBegin(), cend = constituents.constEnd();
                    for ( ; cit != cend; ++cit) {
                        const quint32 cid((*cit).first);
                        const QString cst((*cit).second);

                        if (cst == syncTarget || cst == wasLocalSyncTarget) {
                            if (cst == syncTarget) {
                                // Do not include any constituents from this sync service that are not the base itself
                                // (Note that the base will either be from this service, or a local where there is no
                                // constituent from this service)
                                if (cid != baseId) {
                                    continue;
                                }
                            }
                            if (const QContact *constituent = constituentContacts[cid]) {
                                promoteDetailsToAggregate(*constituent, &partialAggregate, DetailList());
                            } else {
                                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to promote details from missing constitutent: %1").arg(cid));
                                return QContactManager::UnspecifiedError;
                            }
                        }
                    }

                    if (addedAggregateIds.contains(aggId)) {
                        addedContacts->append(partialAggregate);
                    } else {
                        syncContacts->append(partialAggregate);
                    }
                }
            }
        }
    }

    if (deletedContactIds) {
        m_deletedSyncContactIds.bindValue(":lastSync", since);
        if (!m_deletedSyncContactIds.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch sync contact ids:\n%1")
                    .arg(m_deletedSyncContactIds.lastError().text()));
            return QContactManager::UnspecifiedError;
        }
        while (m_deletedSyncContactIds.next()) {
            const quint32 dbId(m_deletedSyncContactIds.value(0).toUInt());
            const QString st(m_deletedSyncContactIds.value(1).toString());

            // If this contact was from this source, or was exported to this source, report the deletion
            if (st == syncTarget || exportedIds.contains(dbId)) {
                deletedContactIds->append(ContactId::apiId(dbId));
            }
        }
        m_deletedSyncContactIds.finish();
    }

    return QContactManager::NoError;
}

static void modifyContactDetail(const QContactDetail &original, const QContactDetail &modified,
                                QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                QContactDetail *recipient)
{
    // Apply changes field-by-field
    DetailMap originalValues(detailValues(original, false));
    DetailMap modifiedValues(detailValues(modified, false));

    DetailMap::const_iterator mit = modifiedValues.constBegin(), mend = modifiedValues.constEnd();
    for ( ; mit != mend; ++mit) {
        const int field(mit.key());
        const QVariant modifiedValue(mit.value());

        const QVariant originalValue(originalValues[field]);
        originalValues.remove(field);

        const QVariant currentValue(recipient->value(field));
        if (!variantEqual(currentValue, originalValue)) {
            // The local value has changed since this data was exported
            if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges) {
                // Ignore this remote change
                continue;
            }
        }

        // Update the result value
        recipient->setValue(field, mit.value());
    }

    DetailMap::const_iterator oit = originalValues.constBegin(), oend = originalValues.constEnd();
    for ( ; oit != oend; ++oit) {
        // Any previously existing values that are no longer present should be removed
        const int field(oit.key());
        const QVariant originalValue(oit.value());

        const QVariant currentValue(recipient->value(field));
        if (!variantEqual(currentValue, originalValue)) {
            // The local value has changed since this data was exported
            if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges) {
                // Ignore this remote removal
                continue;
            }
        }

        recipient->removeValue(field);
    }
}

QContactManager::Error ContactWriter::syncUpdate(const QString &syncTarget,
                                                 QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                                 const QList<QPair<QContact, QContact> > &remoteChanges)
{
    static const ContactWriter::DetailList compositionDetailTypes(getCompositionDetailTypes());

    QMap<quint32, QList<QPair<StringPair, DetailPair> > > contactModifications;
    QMap<quint32, QList<QContactDetail> > contactAdditions;
    QMap<quint32, QList<StringPair> > contactRemovals;

    QSet<quint32> compositionModificationIds;

    // For each pair of contacts, determine the changes to be applied
    QList<QPair<QContact, QContact> >::const_iterator cit = remoteChanges.constBegin(), cend = remoteChanges.constEnd();
    for ( ; cit != cend; ++cit) {
        const QPair<QContact, QContact> &pair(*cit);

        const QContact &original(pair.first);
        const QContact &updated(pair.second);

        const quint32 contactId(ContactId::databaseId(original.id()));
        const quint32 updatedId(ContactId::databaseId(updated.id()));

        if (updatedId != contactId) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Mismatch on sync contact ids:\n%1 != %2")
                    .arg(updatedId).arg(contactId));
            return QContactManager::UnspecifiedError;
        }

        // Extract the details from these contacts
        QList<QPair<QContactDetail, StringPair> > originalDetails(contactDetails(original));
        QList<QPair<QContactDetail, StringPair> > updatedDetails(contactDetails(updated));

        // Remove any details that are equivalent in both contacts
        removeEquivalentDetails(originalDetails, updatedDetails);

        if (originalDetails.isEmpty() && updatedDetails.isEmpty()) {
            continue;
        }

        // See if any of these differences are in-place modifications
        QList<QPair<QContactDetail, StringPair> >::const_iterator uit = updatedDetails.constBegin(), uend = updatedDetails.constEnd();
        for ( ; uit != uend; ++uit) {
            const QContactDetail &detail((*uit).first);
            const StringPair &identity((*uit).second);

            if (identity.first.isEmpty()) {
                const QContactDetail::DetailType type(detailType(detail));
                if (compositionDetailTypes.contains(type)) {
                    // This is a modification of a contacts table detail - we will eventually work out the new composition
                    contactModifications[contactId].append(qMakePair(identity, qMakePair(original.detail(type), detail)));
                    compositionModificationIds.insert(contactId);
                } else {
                    // This is a new detail altogether
                    contactAdditions[contactId].append(detail);
                }
            } else {
                QList<QPair<QContactDetail, StringPair> >::iterator oit = originalDetails.begin(), oend = originalDetails.end();
                for ( ; oit != oend; ++oit) {
                    if ((*oit).second == identity) {
                        // This detail is present in the original contact; the new version is a modification

                        // Find the contact where this detail originates
                        const QStringList provenance(identity.first.split(QChar::fromLatin1(':')));
                        const quint32 originId(provenance.at(0).toUInt());

                        contactModifications[originId].append(qMakePair(identity, qMakePair((*oit).first, detail)));
                        originalDetails.erase(oit);
                        break;
                    }
                }
                if (oit == oend) {
                    // The original detail for this modification cannot be found
                    if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveRemoteChanges) {
                        // Handle the updated value as an addition
                        contactAdditions[contactId].append(detail);
                    }
                }
            }
        }

        // Any remaining (non-composition) original details must be removed
        QList<QPair<QContactDetail, StringPair> >::const_iterator oit = originalDetails.constBegin(), oend = originalDetails.constEnd();
        for ( ; oit != oend; ++oit) {
            const StringPair &identity((*oit).second);
            if (!identity.first.isEmpty()) {
                contactRemovals[contactId].append(identity);
            }
        }
    }

    if (!compositionModificationIds.isEmpty()) {
        // We also need to modify the composited details for the local constituents of these contacts
        QVariantList ids;
        foreach (quint32 id, compositionModificationIds) {
            ids.append(id);
        }

        ContactsDatabase::clearTemporaryContactIdsTable(m_database, syncConstituentsTable);

        if (!ContactsDatabase::createTemporaryContactIdsTable(m_database, syncConstituentsTable, ids)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncConstituents temporary table"));
            return QContactManager::UnspecifiedError;
        }

        if (!m_localConstituentIds.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch local constituent ids for sync update:\n%1")
                    .arg(m_localConstituentIds.lastError().text()));
            return QContactManager::UnspecifiedError;
        }
        while (m_localConstituentIds.next()) {
            const quint32 localConstituentId = m_localConstituentIds.value(0).toUInt();
            const quint32 modifiedConstituentId = m_localConstituentIds.value(1).toUInt();

            if (localConstituentId != modifiedConstituentId) {
                // Any changes made to the constituent must also be made to the local to be effective
                contactModifications[localConstituentId].append(contactModifications[modifiedConstituentId]);
            }
        }
        m_localConstituentIds.finish();
    }

    QList<quint32> affectedContactIds(contactModifications.keys() + contactAdditions.keys() + contactRemovals.keys());
    std::sort(affectedContactIds.begin(), affectedContactIds.end());
    QList<quint32>::iterator newEnd = std::unique(affectedContactIds.begin(), affectedContactIds.end());
    affectedContactIds.erase(newEnd, affectedContactIds.end());

    // Fetch all the contacts we want to apply modifications to
    if (!affectedContactIds.isEmpty()) {
        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("syncUpdate"), &readList, affectedContactIds, hint);
        if (readError != QContactManager::NoError || readList.size() != affectedContactIds.size()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read contacts for sync update"));
            return QContactManager::UnspecifiedError;
        }

        QList<QContact> modifiedContacts;

        // Create updated versions of the affected contacts
        foreach (QContact contact, readList) {
            const quint32 contactId(ContactId::databaseId(contact.id()));

            const QString cst(contact.detail<QContactSyncTarget>().syncTarget());
            if (cst != syncTarget && cst != localSyncTarget && cst != wasLocalSyncTarget) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid update constituent syncTarget for sync update: %1").arg(cst));
                return QContactManager::UnspecifiedError;
            }

            // Apply the changes for this contact
            QSet<StringPair> removals(contactRemovals.value(contactId).toSet());

            QMap<StringPair, DetailPair> modifications;
            QMap<QContactDetail::DetailType, DetailPair> composedModifications;

            const QList<QPair<StringPair, DetailPair> > &mods(contactModifications.value(contactId));
            QList<QPair<StringPair, DetailPair> >::const_iterator mit = mods.constBegin(), mend = mods.constEnd();
            for ( ; mit != mend; ++mit) {
                const StringPair identity((*mit).first);
                if (identity.first.isEmpty()) {
                    const QContactDetail::DetailType type(detailType((*mit).second.second));
                    composedModifications.insert(type, (*mit).second);
                } else {
                    modifications.insert(identity, (*mit).second);
                }
            }

            foreach (QContactDetail detail, contact.details()) {
                const QString provenance(detail.value(QContactDetail__FieldProvenance).toString());

                if (provenance.isEmpty()) {
                    QMap<QContactDetail::DetailType, DetailPair>::iterator cit = composedModifications.find(detailType(detail));
                    if (cit != composedModifications.end()) {
                        // Apply this modification
                        modifyContactDetail((*cit).first, (*cit).second, conflictPolicy, &detail);
                        contact.saveDetail(&detail);

                        composedModifications.erase(cit);
                    }
                } else {
                    const StringPair detailIdentity(qMakePair(provenance, detailTypeName(detail)));

                    QSet<StringPair>::iterator rit = removals.find(detailIdentity);
                    if (rit != removals.end()) {
                        // Remove this detail from the contact
                        contact.removeDetail(&detail);

                        removals.erase(rit);
                    } else {
                        QMap<StringPair, DetailPair>::iterator mit = modifications.find(detailIdentity);
                        if (mit != modifications.end()) {
                            // Apply the modification to this contact's detail
                            modifyContactDetail((*mit).first, (*mit).second, conflictPolicy, &detail);
                            contact.saveDetail(&detail);

                            modifications.erase(mit);
                        }
                    }
                }
            }

            if (!removals.isEmpty()) {
                // Is there anything that can be done here, for PreserveRemoteChanges?
            }
            if (!modifications.isEmpty()) {
                QMap<StringPair, DetailPair>::const_iterator mit = modifications.constBegin(), mend = modifications.constEnd();
                for ( ; mit != mend; ++mit) {
                    const StringPair identity(mit.key());
                    if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveRemoteChanges) {
                        // Handle the updated value as an addition
                        QContactDetail updated(mit.value().second);
                        contact.saveDetail(&updated);
                    }
                }
            }
            if (!composedModifications.isEmpty()) {
                QMap<QContactDetail::DetailType, DetailPair>::const_iterator cit = composedModifications.constBegin(), cend = composedModifications.constEnd();
                for ( ; cit != cend; ++cit) {
                    // Apply these modifications to empty details, and add to the contact
                    QContactDetail detail = contact.detail(cit.key());
                    modifyContactDetail((*cit).first, (*cit).second, conflictPolicy, &detail);
                    contact.saveDetail(&detail);
                }
            }

            // Store any additions to the contact
            foreach (QContactDetail detail, contactAdditions.value(contactId)) {
                if (cst == syncTarget) {
                    // Sync contact details should be modifiable
                    detail.setValue(QContactDetail__FieldModifiable, true);
                }
                contact.saveDetail(&detail);
            }

            modifiedContacts.append(contact);
        }

        // Store the changes we've accumulated
        QMap<int, QContactManager::Error> errorMap;
        QContactManager::Error writeError = save(&modifiedContacts, DetailList(), 0, &errorMap, true, false, true);
        if (writeError != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save contact changes for sync update"));
            return writeError;
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
    QDateTime modifiedTime = QDateTime::currentDateTimeUtc();

    // always clobber last modified timestamp.
    timestamp.setLastModified(modifiedTime);
    if (setCreationTimestamp && !createdTime.isValid()) {
        timestamp.setCreated(modifiedTime);
    }

    return contact->saveDetail(&timestamp);
}

QContactManager::Error ContactWriter::create(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinAggregateUpdate)
{
#ifndef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    Q_UNUSED(withinTransaction)
    Q_UNUSED(withinAggregateUpdate)
#endif

    // If not specified, this contact is a "local device" contact
    QContactSyncTarget starget = contact->detail<QContactSyncTarget>();
    const QString stv = starget.syncTarget();
    if (stv.isEmpty()) {
        starget.setSyncTarget(localSyncTarget);
        contact->saveDetail(&starget);
    }

    // If this contact is local, ensure it has a GUID for import/export stability
    if (stv.isEmpty() || stv == localSyncTarget) {
        QContactGuid guid = contact->detail<QContactGuid>();
        if (guid.guid().isEmpty()) {
            guid.setGuid(QUuid::createUuid().toString());
            contact->saveDetail(&guid);
        }
    }

    if (definitionMask.isEmpty() ||
        detailListContains<QContactPresence>(definitionMask) ||
        detailListContains<QContactGlobalPresence>(definitionMask)) {
        // update the global presence (display label may be derived from it)
        updateGlobalPresence(contact);
    }

    // update the display label for this contact
    m_engine.regenerateDisplayLabel(*contact);

    // update the timestamp if necessary
    updateTimestamp(contact, true); // set creation timestamp

    QContactManager::Error writeErr = enforceDetailConstraints(contact);
    if (writeErr != QContactManager::NoError) {
        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Contact failed detail constraints"));
        return writeErr;
    }

    bindContactDetails(*contact, m_insertContact);
    if (!m_insertContact.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create contact:\n%1")
                .arg(m_insertContact.lastError().text()));
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
            if (contact->detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget) != aggregateSyncTarget) {
                writeErr = setAggregate(contact, contactId, false, definitionMask, withinTransaction);
                if (writeErr != QContactManager::NoError)
                    return writeErr;
            }
        }
#endif
    }

    if (writeErr != QContactManager::NoError) {
        // error occurred.  Remove the failed entry.
        m_removeContact.bindValue(":contactId", contactId);
        if (!m_removeContact.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove stale contact after failed save:\n%1")
                    .arg(m_removeContact.lastError().text()));
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
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to check contact existence:\n%1")
                .arg(m_checkContactExists.lastError().text()));
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
        (oldSyncTarget != localSyncTarget && oldSyncTarget != wasLocalSyncTarget)) {
        // they are attempting to manually change the sync target value of a non-local contact
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot manually change sync target!"));
        return QContactManager::InvalidDetailError;
    }

    QContactManager::Error writeError = enforceDetailConstraints(contact);
    if (writeError != QContactManager::NoError) {
        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Contact failed detail constraints"));
        return writeError;
    }

    // update the modification timestamp
    updateTimestamp(contact, false);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (!withinAggregateUpdate && oldSyncTarget == aggregateSyncTarget) {
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

    if (definitionMask.isEmpty() ||
        detailListContains<QContactPresence>(definitionMask) ||
        detailListContains<QContactGlobalPresence>(definitionMask)) {
        // update the global presence (display label may be derived from it)
        updateGlobalPresence(contact);
    }

    // update the display label for this contact
    m_engine.regenerateDisplayLabel(*contact);

    bindContactDetails(*contact, m_updateContact, definitionMask, contactId);
    if (!m_updateContact.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to update contact:\n%1")
                .arg(m_updateContact.lastError().text()));
        return QContactManager::UnspecifiedError;
    }
    m_updateContact.finish();

    writeError = write(contactId, contact, definitionMask);

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    if (writeError == QContactManager::NoError) {
        if (oldSyncTarget != aggregateSyncTarget) {
            QList<quint32> aggregatesOfUpdated;
            m_findAggregateForContact.bindValue(":localId", contactId);
            if (!m_findAggregateForContact.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch aggregator contact ids during remove:\n%1")
                        .arg(m_findAggregateForContact.lastError().text()));
                return QContactManager::UnspecifiedError;
            }
            while (m_findAggregateForContact.next()) {
                aggregatesOfUpdated.append(m_findAggregateForContact.value(0).toUInt());
            }
            m_findAggregateForContact.finish();

            if (aggregatesOfUpdated.size() > 0) {
                writeError = regenerateAggregates(aggregatesOfUpdated, definitionMask, withinTransaction);
            } else if (oldSyncTarget != localSyncTarget && oldSyncTarget != wasLocalSyncTarget) {
                writeError = setAggregate(contact, contactId, true, definitionMask, withinTransaction);
            }
            if (writeError != QContactManager::NoError)
                return writeError;

            *aggregateUpdated = true;
        }
    }
#endif

    return writeError;
}

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
QContactManager::Error ContactWriter::setAggregate(QContact *contact, quint32 contactId, bool update, const DetailList &definitionMask, bool withinTransaction)
{
    quint32 aggregateId = 0;

    QContactManager::Error writeErr = updateOrCreateAggregate(contact, definitionMask, withinTransaction, &aggregateId);
    if ((writeErr == QContactManager::NoError) && (update || (aggregateId < contactId))) {
        // The aggregate pre-dates the new contact - it probably had a local constituent already
        quint32 localCount = 0;

        m_countLocalConstituents.bindValue(":aggregateId", aggregateId);
        if (!m_countLocalConstituents.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to count local consitutents for aggregate %1 remove:\n%2")
                    .arg(aggregateId).arg(m_countLocalConstituents.lastError().text()));
            return QContactManager::UnspecifiedError;
        }
        if (m_countLocalConstituents.next()) {
            localCount = m_countLocalConstituents.value(0).toUInt();
        }
        m_countLocalConstituents.finish();

        if (localCount > 1) {
            // The matched aggregate now has multiple 'local' constituents; change this one to 'was_local'
            m_updateSyncTarget.bindValue(":contactId", contactId);
            m_updateSyncTarget.bindValue(":syncTarget", wasLocalSyncTarget);
            if (!m_updateSyncTarget.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to update contact syncTarget:\n%1")
                        .arg(m_updateSyncTarget.lastError().text()));
                return QContactManager::UnspecifiedError;
            }
            m_updateSyncTarget.finish();
        }
    }

    return QContactManager::NoError;
}
#endif

QContactManager::Error ContactWriter::write(quint32 contactId, QContact *contact, const DetailList &definitionMask)
{
    // Is this contact syncable with a syncTarget?
    const QString syncTarget(contact->detail<QContactSyncTarget>().syncTarget());
    const bool wasLocal = (syncTarget == wasLocalSyncTarget);
    const bool syncable = !syncTarget.isEmpty() &&
                          (syncTarget != aggregateSyncTarget) &&
                          (syncTarget != localSyncTarget) &&
                          !wasLocal;

    QContactManager::Error error = QContactManager::NoError;
    if (writeDetails<QContactAddress>(contactId, contact, m_removeAddress, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactAnniversary>(contactId, contact, m_removeAnniversary, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactAvatar>(contactId, contact, m_removeAvatar, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactBirthday>(contactId, contact, m_removeBirthday, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactEmailAddress>(contactId, contact, m_removeEmailAddress, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactGlobalPresence>(contactId, contact, m_removeGlobalPresence, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactGuid>(contactId, contact, m_removeGuid, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactHobby>(contactId, contact, m_removeHobby, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactNickname>(contactId, contact, m_removeNickname, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactNote>(contactId, contact, m_removeNote, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactOnlineAccount>(contactId, contact, m_removeOnlineAccount, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactOrganization>(contactId, contact, m_removeOrganization, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactPhoneNumber>(contactId, contact, m_removePhoneNumber, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactPresence>(contactId, contact, m_removePresence, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactRingtone>(contactId, contact, m_removeRingtone, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactTag>(contactId, contact, m_removeTag, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactUrl>(contactId, contact, m_removeUrl, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactOriginMetadata>(contactId, contact, m_removeOriginMetadata, definitionMask, syncTarget, syncable, wasLocal, &error)
            && writeDetails<QContactExtendedDetail>(contactId, contact, m_removeExtendedDetail, definitionMask, syncTarget, syncable, wasLocal, &error)
            ) {
        return QContactManager::NoError;
    }
    return error;
}

void ContactWriter::bindContactDetails(const QContact &contact, QSqlQuery &query, const DetailList &definitionMask, quint32 contactId)
{
    QContactDisplayLabel label = contact.detail<QContactDisplayLabel>();
    query.bindValue(0, label.label().trimmed());

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
    query.bindValue(8, name.value<QString>(QContactName__FieldCustomLabel).trimmed());

    const QString syncTarget(contact.detail<QContactSyncTarget>().syncTarget());
    query.bindValue(9, syncTarget);

    const QContactTimestamp timestamp = contact.detail<QContactTimestamp>();
    query.bindValue(10, timestamp.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp).toUTC());
    query.bindValue(11, timestamp.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp).toUTC());

    const QContactGender gender = contact.detail<QContactGender>();
    const QString gv(gender.gender() == QContactGender::GenderFemale ? QString::fromLatin1("Female") :
                     gender.gender() == QContactGender::GenderMale ? QString::fromLatin1("Male") : QString());
    query.bindValue(12, gv);

    const QContactFavorite favorite = contact.detail<QContactFavorite>();
    query.bindValue(13, favorite.isFavorite());

    const bool update(contactId != 0);

    // Does this contact contain the information needed to update hasPhoneNumber?
    bool hasPhoneNumberKnown = definitionMask.isEmpty() || detailListContains<QContactPhoneNumber>(definitionMask);
    bool hasPhoneNumber = hasPhoneNumberKnown ? !contact.detail<QContactPhoneNumber>().isEmpty() : false;

    bool hasEmailAddressKnown = definitionMask.isEmpty() || detailListContains<QContactEmailAddress>(definitionMask);
    bool hasEmailAddress = hasEmailAddressKnown ? !contact.detail<QContactEmailAddress>().isEmpty() : false;

    bool hasOnlineAccountKnown = definitionMask.isEmpty() || detailListContains<QContactOnlineAccount>(definitionMask);
    bool hasOnlineAccount = hasOnlineAccountKnown ? !contact.detail<QContactOnlineAccount>().isEmpty() : false;

    // isOnline is true if any presence details are not offline/unknown
    bool isOnlineKnown = definitionMask.isEmpty() || detailListContains<QContactPresence>(definitionMask);
    bool isOnline = false;
    foreach (const QContactPresence &presence, contact.details<QContactPresence>()) {
        if (presence.presenceState() >= QContactPresence::PresenceAvailable &&
            presence.presenceState() <= QContactPresence::PresenceExtendedAway) {
            isOnline = true;
            break;
        }
    }

    // isDeactivated is true if the contact contains QContactDeactivated
    bool isDeactivatedKnown = definitionMask.isEmpty() || detailListContains<QContactDeactivated>(definitionMask);
    bool isDeactivated = isDeactivatedKnown ? !contact.details<QContactDeactivated>().isEmpty() : false;
    if (isDeactivated) {
        // Deactivation is only possible for syncable contacts
        if (syncTarget == aggregateSyncTarget || syncTarget == localSyncTarget || syncTarget == wasLocalSyncTarget) {
            isDeactivated = false;
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot set deactivated for syncTarget: %1").arg(syncTarget));
        }
    }

    if (update) {
        query.bindValue(14, hasPhoneNumberKnown);
        query.bindValue(15, hasPhoneNumber);
        query.bindValue(16, hasEmailAddressKnown);
        query.bindValue(17, hasEmailAddress);
        query.bindValue(18, hasOnlineAccountKnown);
        query.bindValue(19, hasOnlineAccount);
        query.bindValue(20, isOnlineKnown);
        query.bindValue(21, isOnline);
        query.bindValue(22, isDeactivatedKnown);
        query.bindValue(23, isDeactivated);
        query.bindValue(24, contactId);
    } else {
        query.bindValue(14, hasPhoneNumber);
        query.bindValue(15, hasEmailAddress);
        query.bindValue(16, hasOnlineAccount);
        query.bindValue(17, isOnline);
        query.bindValue(18, isDeactivated);

        // Incidental state only applies to creation
        query.bindValue(19, !contact.details<QContactIncidental>().isEmpty());
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
    m_insertAddress.bindValue(7, Address::subTypeList(detail.subTypes()).join(QLatin1String(";")));
    return m_insertAddress;
}

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactAnniversary &detail)
{
    typedef QContactAnniversary T;
    m_insertAnniversary.bindValue(0, contactId);
    m_insertAnniversary.bindValue(1, detailValue(detail, T::FieldOriginalDate));
    m_insertAnniversary.bindValue(2, detailValue(detail, T::FieldCalendarId));
    m_insertAnniversary.bindValue(3, Anniversary::subType(detail.subType()));
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
    m_insertOnlineAccount.bindValue(3, OnlineAccount::protocol(detail.protocol()));
    m_insertOnlineAccount.bindValue(4, detailValue(detail, T::FieldServiceProvider));
    m_insertOnlineAccount.bindValue(5, detailValue(detail, T::FieldCapabilities).value<QStringList>().join(QLatin1String(";")));
    m_insertOnlineAccount.bindValue(6, OnlineAccount::subTypeList(detail.subTypes()).join(QLatin1String(";")));
    m_insertOnlineAccount.bindValue(7, detailValue(detail, QContactOnlineAccount__FieldAccountPath));
    m_insertOnlineAccount.bindValue(8, detailValue(detail, QContactOnlineAccount__FieldAccountIconPath));
    m_insertOnlineAccount.bindValue(9, detailValue(detail, QContactOnlineAccount__FieldEnabled));
    m_insertOnlineAccount.bindValue(10, detailValue(detail, QContactOnlineAccount__FieldAccountDisplayName));
    m_insertOnlineAccount.bindValue(11, detailValue(detail, QContactOnlineAccount__FieldServiceProviderDisplayName));
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
    m_insertPhoneNumber.bindValue(2, PhoneNumber::subTypeList(detail.subTypes()).join(QLatin1String(";")));
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
    m_insertUrl.bindValue(2, Url::subType(detail.subType()));
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

QSqlQuery &ContactWriter::bindDetail(quint32 contactId, const QContactExtendedDetail &detail)
{
    m_insertExtendedDetail.bindValue(0, contactId);
    m_insertExtendedDetail.bindValue(1, detailValue(detail, QContactExtendedDetail::FieldName));
    m_insertExtendedDetail.bindValue(2, detailValue(detail, QContactExtendedDetail::FieldData));
    return m_insertExtendedDetail;
}

