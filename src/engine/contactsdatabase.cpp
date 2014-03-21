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

#include "contactsdatabase.h"
#include "trace_p.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

#include <QtDebug>

Q_GLOBAL_STATIC_WITH_ARGS(QMutex, databaseMutex, (QMutex::Recursive));

static const char *setupEncoding =
        "\n PRAGMA encoding = \"UTF-16\";";

static const char *setupTempStore =
        "\n PRAGMA temp_store = MEMORY;";

static const char *setupJournal =
        "\n PRAGMA journal_mode = WAL;";

static const char *setupSynchronous =
        "\n PRAGMA synchronous = FULL;";

static const char *createContactsTable =
        "\n CREATE TABLE Contacts ("
        "\n contactId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n displayLabel TEXT,"
        "\n firstName TEXT,"
        "\n lowerFirstName TEXT,"
        "\n lastName TEXT,"
        "\n lowerLastName TEXT,"
        "\n middleName TEXT,"
        "\n prefix TEXT,"
        "\n suffix TEXT,"
        "\n customLabel TEXT,"
        "\n syncTarget TEXT NOT NULL,"
        "\n created DATETIME,"
        "\n modified DATETIME,"
        "\n gender TEXT,"
        "\n isFavorite BOOL,"
        "\n hasPhoneNumber BOOL DEFAULT 0,"
        "\n hasEmailAddress BOOL DEFAULT 0,"
        "\n hasOnlineAccount BOOL DEFAULT 0,"
        "\n isOnline BOOL DEFAULT 0,"
        "\n isDeactivated BOOL DEFAULT 0,"
        "\n isIncidental BOOL DEFAULT 0,"
        "\n type INTEGER DEFAULT 0);"; // QContactType::TypeContact

static const char *createAddressesTable =
        "\n CREATE TABLE Addresses ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY ASC,"
        "\n street TEXT,"
        "\n postOfficeBox TEXT,"
        "\n region TEXT,"
        "\n locality TEXT,"
        "\n postCode TEXT,"
        "\n country TEXT,"
        "\n subTypes TEXT);";

static const char *createAnniversariesTable =
        "\n CREATE TABLE Anniversaries ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n originalDateTime DATETIME,"
        "\n calendarId TEXT,"
        "\n subType TEXT);";

static const char *createAvatarsTable =
        "\n CREATE TABLE Avatars ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n imageUrl TEXT,"
        "\n videoUrl TEXT,"
        "\n avatarMetadata TEXT);"; // arbitrary metadata

static const char *createBirthdaysTable =
        "\n CREATE TABLE Birthdays ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n birthday DATETIME,"
        "\n calendarId TEXT);";

static const char *createEmailAddressesTable =
        "\n CREATE TABLE EmailAddresses ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n emailAddress TEXT,"
        "\n lowerEmailAddress TEXT);";

static const char *createGlobalPresencesTable =
        "\n CREATE TABLE GlobalPresences ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n presenceState INTEGER,"
        "\n timestamp DATETIME,"
        "\n nickname TEXT,"
        "\n customMessage TEXT);";

static const char *createGuidsTable =
        "\n CREATE TABLE Guids ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n guid TEXT);";

static const char *createHobbiesTable =
        "\n CREATE TABLE Hobbies ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n hobby TEXT);";

static const char *createNicknamesTable =
        "\n CREATE TABLE Nicknames ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n nickname TEXT,"
        "\n lowerNickname TEXT);";

static const char *createNotesTable =
        "\n CREATE TABLE Notes ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n note TEXT);";

static const char *createOnlineAccountsTable =
        "\n CREATE TABLE OnlineAccounts ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n accountUri TEXT,"
        "\n lowerAccountUri TEXT,"
        "\n protocol TEXT,"
        "\n serviceProvider TEXT,"
        "\n capabilities TEXT,"
        "\n subTypes TEXT,"
        "\n accountPath TEXT,"
        "\n accountIconPath TEXT,"
        "\n enabled BOOL,"
        "\n accountDisplayName TEXT,"
        "\n serviceProviderDisplayName TEXT);";

static const char *createOrganizationsTable =
        "\n CREATE TABLE Organizations ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n name TEXT,"
        "\n role TEXT,"
        "\n title TEXT,"
        "\n location TEXT,"
        "\n department TEXT,"
        "\n logoUrl TEXT);";

static const char *createPhoneNumbersTable =
        "\n CREATE TABLE PhoneNumbers ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n phoneNumber TEXT,"
        "\n subTypes TEXT,"
        "\n normalizedNumber TEXT);";

static const char *createPresencesTable =
        "\n CREATE TABLE Presences ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n presenceState INTEGER,"
        "\n timestamp DATETIME,"
        "\n nickname TEXT,"
        "\n customMessage TEXT);";

static const char *createRingtonesTable =
        "\n CREATE TABLE Ringtones ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n audioRingtone TEXT,"
        "\n videoRingtone TEXT);";

static const char *createTagsTable =
        "\n CREATE TABLE Tags ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n tag TEXT);";

static const char *createUrlsTable =
        "\n CREATE TABLE Urls ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n url TEXT,"
        "\n subTypes TEXT);";

static const char *createTpMetadataTable =
        "\n CREATE TABLE TpMetadata ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n telepathyId TEXT,"
        "\n accountId TEXT,"
        "\n accountEnabled BOOL);";

static const char *createExtendedDetailsTable =
        "\n CREATE TABLE ExtendedDetails ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n name TEXT,"
        "\n data BLOB);";

static const char *createDetailsTable =
        "\n CREATE TABLE Details ("
        "\n contactId INTEGER KEY,"
        "\n detailId INTEGER,"
        "\n detail TEXT,"
        "\n detailUri TEXT UNIQUE,"
        "\n linkedDetailUris TEXT,"
        "\n contexts TEXT,"
        "\n accessConstraints INTEGER,"
        "\n provenance TEXT,"
        "\n modifiable BOOL);";

static const char *createDetailsJoinIndex =
        "\n CREATE INDEX DetailsJoinIndex ON Details(detailId, detail);";

static const char *createDetailsRemoveIndex =
        "\n CREATE INDEX DetailsRemoveIndex ON Details(contactId, detail);";

static const char *createAddressesDetailsContactIdIndex =
        "\n CREATE INDEX createAddressesDetailsContactIdIndex ON Addresses(contactId);";
static const char *createAnniversariesDetailsContactIdIndex =
        "\n CREATE INDEX createAnniversariesDetailsContactIdIndex ON Anniversaries(contactId);";
static const char *createAvatarsDetailsContactIdIndex =
        "\n CREATE INDEX createAvatarsDetailsContactIdIndex ON Avatars(contactId);";
static const char *createBirthdaysDetailsContactIdIndex =
        "\n CREATE INDEX createBirthdaysDetailsContactIdIndex ON Birthdays(contactId);";
static const char *createEmailAddressesDetailsContactIdIndex =
        "\n CREATE INDEX createEmailAddressesDetailsContactIdIndex ON EmailAddresses(contactId);";
static const char *createGlobalPresencesDetailsContactIdIndex =
        "\n CREATE INDEX createGlobalPresencesDetailsContactIdIndex ON GlobalPresences(contactId);";
static const char *createGuidsDetailsContactIdIndex =
        "\n CREATE INDEX createGuidsDetailsContactIdIndex ON Guids(contactId);";
static const char *createHobbiesDetailsContactIdIndex =
        "\n CREATE INDEX createHobbiesDetailsContactIdIndex ON Hobbies(contactId);";
static const char *createNicknamesDetailsContactIdIndex =
        "\n CREATE INDEX createNicknamesDetailsContactIdIndex ON Nicknames(contactId);";
static const char *createNotesDetailsContactIdIndex =
        "\n CREATE INDEX createNotesDetailsContactIdIndex ON Notes(contactId);";
static const char *createOnlineAccountsDetailsContactIdIndex =
        "\n CREATE INDEX createOnlineAccountsDetailsContactIdIndex ON OnlineAccounts(contactId);";
static const char *createOrganizationsDetailsContactIdIndex =
        "\n CREATE INDEX createOrganizationsDetailsContactIdIndex ON Organizations(contactId);";
static const char *createPhoneNumbersDetailsContactIdIndex =
        "\n CREATE INDEX createPhoneNumbersDetailsContactIdIndex ON PhoneNumbers(contactId);";
static const char *createPresencesDetailsContactIdIndex =
        "\n CREATE INDEX createPresencesDetailsContactIdIndex ON Presences(contactId);";
static const char *createRingtonesDetailsContactIdIndex =
        "\n CREATE INDEX createRingtonesDetailsContactIdIndex ON Ringtones(contactId);";
static const char *createTagsDetailsContactIdIndex =
        "\n CREATE INDEX createTagsDetailsContactIdIndex ON Tags(contactId);";
static const char *createUrlsDetailsContactIdIndex =
        "\n CREATE INDEX createUrlsDetailsContactIdIndex ON Urls(contactId);";
static const char *createTpMetadataDetailsContactIdIndex =
        "\n CREATE INDEX createTpMetadataDetailsContactIdIndex ON TpMetadata(contactId);";
static const char *createExtendedDetailsContactIdIndex =
        "\n CREATE INDEX createExtendedDetailsContactIdIndex ON ExtendedDetails(contactId);";

static const char *createIdentitiesTable =
        "\n CREATE Table Identities ("
        "\n identity INTEGER PRIMARY KEY,"
        "\n contactId INTEGER KEY);";

static const char *createRelationshipsTable =
        "\n CREATE Table Relationships ("
        "\n firstId INTEGER NOT NULL,"
        "\n secondId INTEGER NOT NULL,"
        "\n type TEXT,"
        "\n PRIMARY KEY (firstId, secondId, type));";

static const char *createDeletedContactsTable =
        "\n CREATE TABLE DeletedContacts ("
        "\n contactId INTEGER PRIMARY KEY,"
        "\n syncTarget TEXT,"
        "\n deleted DATETIME);";

static const char *createOOBTable =
        "\n CREATE TABLE OOB ("
        "\n name TEXT PRIMARY KEY,"
        "\n value BLOB);";

static const char *createRemoveTrigger =
        "\n CREATE TRIGGER RemoveContactDetails"
        "\n BEFORE DELETE"
        "\n ON Contacts"
        "\n BEGIN"
        "\n  INSERT INTO DeletedContacts (contactId, syncTarget, deleted) VALUES (old.contactId, old.syncTarget, strftime('%Y-%m-%dT%H:%M:%SZ', 'now'));"
        "\n  DELETE FROM Addresses WHERE contactId = old.contactId;"
        "\n  DELETE FROM Anniversaries WHERE contactId = old.contactId;"
        "\n  DELETE FROM Avatars WHERE contactId = old.contactId;"
        "\n  DELETE FROM Birthdays WHERE contactId = old.contactId;"
        "\n  DELETE FROM EmailAddresses WHERE contactId = old.contactId;"
        "\n  DELETE FROM GlobalPresences WHERE contactId = old.contactId;"
        "\n  DELETE FROM Guids WHERE contactId = old.contactId;"
        "\n  DELETE FROM Hobbies WHERE contactId = old.contactId;"
        "\n  DELETE FROM Nicknames WHERE contactId = old.contactId;"
        "\n  DELETE FROM Notes WHERE contactId = old.contactId;"
        "\n  DELETE FROM OnlineAccounts WHERE contactId = old.contactId;"
        "\n  DELETE FROM Organizations WHERE contactId = old.contactId;"
        "\n  DELETE FROM PhoneNumbers WHERE contactId = old.contactId;"
        "\n  DELETE FROM Presences WHERE contactId = old.contactId;"
        "\n  DELETE FROM Ringtones WHERE contactId = old.contactId;"
        "\n  DELETE FROM Tags WHERE contactId = old.contactId;"
        "\n  DELETE FROM Urls WHERE contactId = old.contactId;"
        "\n  DELETE FROM TpMetadata WHERE contactId = old.contactId;"
        "\n  DELETE FROM ExtendedDetails WHERE contactId = old.contactId;"
        "\n  DELETE FROM Details WHERE contactId = old.contactId;"
        "\n  DELETE FROM Identities WHERE contactId = old.contactId;"
        "\n  DELETE FROM Relationships WHERE firstId = old.contactId OR secondId = old.contactId;"
        "\n END;";

#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
static const char *createLocalSelfContact =
        "\n INSERT INTO Contacts ("
        "\n contactId,"
        "\n displayLabel,"
        "\n firstName,"
        "\n lowerFirstName,"
        "\n lastName,"
        "\n lowerLastName,"
        "\n middleName,"
        "\n prefix,"
        "\n suffix,"
        "\n customLabel,"
        "\n syncTarget,"
        "\n created,"
        "\n modified,"
        "\n gender,"
        "\n isFavorite)"
        "\n VALUES ("
        "\n 1,"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n 'local',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n 0);";
static const char *createAggregateSelfContact =
        "\n INSERT INTO Contacts ("
        "\n contactId,"
        "\n displayLabel,"
        "\n firstName,"
        "\n lowerFirstName,"
        "\n lastName,"
        "\n lowerLastName,"
        "\n middleName,"
        "\n prefix,"
        "\n suffix,"
        "\n customLabel,"
        "\n syncTarget,"
        "\n created,"
        "\n modified,"
        "\n gender,"
        "\n isFavorite)"
        "\n VALUES ("
        "\n 2,"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n 'aggregate',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n 0);";
static const char *createSelfContactRelationship =
        "\n INSERT INTO Relationships (firstId, secondId, type) VALUES (2, 1, 'Aggregates');";
#else
static const char *createLocalSelfContact =
        "\n INSERT INTO Contacts ("
        "\n contactId,"
        "\n displayLabel,"
        "\n firstName,"
        "\n lowerFirstName,"
        "\n lastName,"
        "\n lowerLastName,"
        "\n middleName,"
        "\n prefix,"
        "\n suffix,"
        "\n customLabel,"
        "\n syncTarget,"
        "\n created,"
        "\n modified,"
        "\n gender,"
        "\n isFavorite)"
        "\n VALUES ("
        "\n 2,"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n 'local',"
        "\n '',"
        "\n '',"
        "\n '',"
        "\n 0);";
#endif

static const char *createContactsSyncTargetIndex =
        "\n CREATE INDEX ContactsSyncTargetIndex ON Contacts(syncTarget);";

static const char *createContactsFirstNameIndex =
        "\n CREATE INDEX ContactsFirstNameIndex ON Contacts(lowerFirstName);";

static const char *createContactsLastNameIndex =
        "\n CREATE INDEX ContactsLastNameIndex ON Contacts(lowerLastName);";

static const char *createContactsModifiedIndex =
        "\n CREATE INDEX ContactsModifiedIndex ON Contacts(modified);";

static const char *createContactsIsFavoriteIndex =
        "\n CREATE INDEX ContactsIsFavoriteIndex ON Contacts(isFavorite);";

static const char *createContactsHasPhoneNumberIndex =
        "\n CREATE INDEX ContactsHasPhoneNumberIndex ON Contacts(hasPhoneNumber);";

static const char *createContactsHasEmailAddressIndex =
        "\n CREATE INDEX ContactsHasEmailAddressIndex ON Contacts(hasEmailAddress);";

static const char *createContactsHasOnlineAccountIndex =
        "\n CREATE INDEX ContactsHasOnlineAccountIndex ON Contacts(hasOnlineAccount);";

static const char *createContactsIsOnlineIndex =
        "\n CREATE INDEX ContactsIsOnlineIndex ON Contacts(isOnline);";

static const char *createContactsIsDeactivatedIndex =
        "\n CREATE INDEX ContactsIsDeactivatedIndex ON Contacts(isDeactivated);";

static const char *createContactsTypeIndex =
        "\n CREATE INDEX ContactsTypeIndex ON Contacts(type);";

static const char *createRelationshipsFirstIdIndex =
        "\n CREATE INDEX RelationshipsFirstIdIndex ON Relationships(firstId);";

static const char *createRelationshipsSecondIdIndex =
        "\n CREATE INDEX RelationshipsSecondIdIndex ON Relationships(secondId);";

static const char *createDeletedContactsDeletedIndex =
        "\n CREATE INDEX DeletedContactsDeletedIndex ON DeletedContacts(deleted);";

static const char *createPhoneNumbersIndex =
        "\n CREATE INDEX PhoneNumbersIndex ON PhoneNumbers(normalizedNumber);";

static const char *createEmailAddressesIndex =
        "\n CREATE INDEX EmailAddressesIndex ON EmailAddresses(lowerEmailAddress);";

static const char *createOnlineAccountsIndex =
        "\n CREATE INDEX OnlineAccountsIndex ON OnlineAccounts(lowerAccountUri);";

static const char *createNicknamesIndex =
        "\n CREATE INDEX NicknamesIndex ON Nicknames(lowerNickname);";

static const char *createTpMetadataTelepathyIdIndex =
        "\n CREATE INDEX TpMetadataTelepathyIdIndex ON TpMetadata(telepathyId);";

static const char *createTpMetadataAccountIdIndex =
        "\n CREATE INDEX TpMetadataAccountIdIndex ON TpMetadata(accountId);";

static const char *createStatements[] =
{
    createContactsTable,
    createAddressesTable,
    createAnniversariesTable,
    createAvatarsTable,
    createBirthdaysTable,
    createEmailAddressesTable,
    createGlobalPresencesTable,
    createGuidsTable,
    createHobbiesTable,
    createNicknamesTable,
    createNotesTable,
    createOnlineAccountsTable,
    createOrganizationsTable,
    createPhoneNumbersTable,
    createPresencesTable,
    createRingtonesTable,
    createTagsTable,
    createUrlsTable,
    createTpMetadataTable,
    createExtendedDetailsTable,
    createDetailsTable,
    createDetailsJoinIndex,
    createDetailsRemoveIndex,
    createAddressesDetailsContactIdIndex,
    createAnniversariesDetailsContactIdIndex,
    createAvatarsDetailsContactIdIndex,
    createBirthdaysDetailsContactIdIndex,
    createEmailAddressesDetailsContactIdIndex,
    createGlobalPresencesDetailsContactIdIndex,
    createGuidsDetailsContactIdIndex,
    createHobbiesDetailsContactIdIndex,
    createNicknamesDetailsContactIdIndex,
    createNotesDetailsContactIdIndex,
    createOnlineAccountsDetailsContactIdIndex,
    createOrganizationsDetailsContactIdIndex,
    createPhoneNumbersDetailsContactIdIndex,
    createPresencesDetailsContactIdIndex,
    createRingtonesDetailsContactIdIndex,
    createTagsDetailsContactIdIndex,
    createUrlsDetailsContactIdIndex,
    createTpMetadataDetailsContactIdIndex,
    createExtendedDetailsContactIdIndex,
    createIdentitiesTable,
    createRelationshipsTable,
    createDeletedContactsTable,
    createOOBTable,
    createRemoveTrigger,
    createLocalSelfContact,
#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    createAggregateSelfContact,
    createSelfContactRelationship,
#endif
    createContactsSyncTargetIndex,
    createContactsFirstNameIndex,
    createContactsLastNameIndex,
    createRelationshipsFirstIdIndex,
    createRelationshipsSecondIdIndex,
    createDeletedContactsDeletedIndex,
    createPhoneNumbersIndex,
    createEmailAddressesIndex,
    createOnlineAccountsIndex,
    createNicknamesIndex,
    createTpMetadataTelepathyIdIndex,
    createTpMetadataAccountIdIndex,
    createContactsModifiedIndex,
    createContactsIsFavoriteIndex,
    createContactsHasPhoneNumberIndex,
    createContactsHasEmailAddressIndex,
    createContactsHasOnlineAccountIndex,
    createContactsIsOnlineIndex,
    createContactsIsDeactivatedIndex,
    createContactsTypeIndex,
};

// Upgrade statement indexed by old version
static const char *upgradeVersion0[] = {
    createContactsModifiedIndex,
    createContactsIsFavoriteIndex,
    createContactsHasPhoneNumberIndex,
    createContactsHasEmailAddressIndex,
    createContactsHasOnlineAccountIndex,
    createContactsIsOnlineIndex,
    "PRAGMA user_version=1",
    0 // NULL-terminated
};
static const char *upgradeVersion1[] = {
    createDeletedContactsTable,
    createDeletedContactsDeletedIndex,
    "DROP TRIGGER RemoveContactDetails",
    createRemoveTrigger,
    "PRAGMA user_version=2",
    0 // NULL-terminated
};
static const char *upgradeVersion2[] = {
    "ALTER TABLE Contacts ADD COLUMN isDeactivated BOOL DEFAULT 0",
    createContactsIsDeactivatedIndex,
    "PRAGMA user_version=3",
    0 // NULL-terminated
};
static const char *upgradeVersion3[] = {
    "ALTER TABLE Contacts ADD COLUMN isIncidental BOOL DEFAULT 0",
    "PRAGMA user_version=4",
    0 // NULL-terminated
};
static const char *upgradeVersion4[] = {
    createOOBTable,
    "PRAGMA user_version=5",
    0 // NULL-terminated
};
static const char *upgradeVersion5[] = {
    // Create the isDeactivated index, if it was previously missed
    "CREATE INDEX IF NOT EXISTS ContactsIsDeactivatedIndex ON Contacts(isDeactivated)",
    "ALTER TABLE Contacts ADD COLUMN type INTEGER DEFAULT 0",
    createContactsTypeIndex,
    "PRAGMA user_version=6",
    0 // NULL-terminated
};

static const char **upgradeVersions[] = {
    upgradeVersion0,
    upgradeVersion1,
    upgradeVersion2,
    upgradeVersion3,
    upgradeVersion4,
    upgradeVersion5,
};

static const int currentSchemaVersion = 6;

static bool execute(QSqlDatabase &database, const QString &statement)
{
    QSqlQuery query(database);
    if (!query.exec(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Query failed: %1\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return false;
    } else {
        return true;
    }
}

static bool beginTransaction(QSqlDatabase &database)
{
    // Use immediate lock acquisition; we should already have an IPC lock, so
    // there will be no lock contention with other writing processes
    return execute(database, QString::fromLatin1("BEGIN IMMEDIATE TRANSACTION"));
}

static bool commitTransaction(QSqlDatabase &database)
{
    return execute(database, QString::fromLatin1("COMMIT TRANSACTION"));
}

static bool rollbackTransaction(QSqlDatabase &database)
{
    return execute(database, QString::fromLatin1("ROLLBACK TRANSACTION"));
}

static bool finalizeTransaction(QSqlDatabase &database, bool success)
{
    if (success) {
        return commitTransaction(database);
    }

    rollbackTransaction(database);
    return false;
}

template <typename T> static int lengthOf(T) { return 0; }
template <typename T, int N> static int lengthOf(const T(&)[N]) { return N; }

static bool executeUpgradeStatements(QSqlDatabase &database)
{
    // Check that the defined schema matches the array of upgrade scripts
    if (currentSchemaVersion != lengthOf(upgradeVersions)) {
        qWarning() << "Invalid schema version:" << currentSchemaVersion;
        return false;
    }

    QSqlQuery versionQuery(database);
    versionQuery.prepare("PRAGMA user_version");
    if (!versionQuery.exec() || !versionQuery.next()) {
        qWarning() << "User version query failed:" << versionQuery.lastError();
        return false;
    }

    int schemaVersion = versionQuery.value(0).toInt();
    versionQuery.finish();

    while (schemaVersion < currentSchemaVersion) {
        qWarning() << "Upgrading contacts database from schema version" << schemaVersion;

        for (unsigned i = 0; upgradeVersions[schemaVersion][i]; i++) {
            if (!execute(database, QLatin1String(upgradeVersions[schemaVersion][i])))
                return false;
        }

        if (!versionQuery.exec() || !versionQuery.next()) {
            qWarning() << "User version query failed:" << versionQuery.lastError();
            return false;
        }

        int version = versionQuery.value(0).toInt();
        versionQuery.finish();

        if (version <= schemaVersion) {
            qWarning() << "Contacts database schema upgrade cycle detected - aborting";
            return false;
        } else {
            schemaVersion = version;
        }
    }

    if (schemaVersion > currentSchemaVersion) {
        qWarning() << "Contacts database schema is newer than expected - this may result in failures or corruption";
    }

    return true;
}

static bool upgradeDatabase(QSqlDatabase &database)
{
    if (!beginTransaction(database))
        return false;

    bool success = executeUpgradeStatements(database);

    return finalizeTransaction(database, success);
}

static bool configureDatabase(QSqlDatabase &database)
{
    if (!execute(database, QLatin1String(setupEncoding))
        || !execute(database, QLatin1String(setupTempStore))
        || !execute(database, QLatin1String(setupJournal))
        || !execute(database, QLatin1String(setupSynchronous))) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to configure contacts database: %1")
                .arg(database.lastError().text()));
        return false;
    }

    return true;
}

static bool executeCreationStatements(QSqlDatabase &database)
{
    for (int i = 0; i < lengthOf(createStatements); ++i) {
        QSqlQuery query(database);

        if (!query.exec(QLatin1String(createStatements[i]))) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Database creation failed: %1\n%2")
                    .arg(query.lastError().text())
                    .arg(createStatements[i]));
            return false;
        }
    }

    if (!execute(database, QString::fromLatin1("PRAGMA user_version=%1").arg(currentSchemaVersion))) {
        return false;
    }

    return true;
}

static bool prepareDatabase(QSqlDatabase &database)
{
    if (!configureDatabase(database))
        return false;

    if (!beginTransaction(database))
        return false;

    bool success = executeCreationStatements(database);

    return finalizeTransaction(database, success);
}

template<typename ValueContainer>
static void debugFilterExpansion(const QString &description, const QString &query, const ValueContainer &bindings)
{
    static const bool debugFilters = !qgetenv("QTCONTACTS_SQLITE_DEBUG_FILTERS").isEmpty();

    if (debugFilters) {
        qDebug() << description << ContactsDatabase::expandQuery(query, bindings);
    }
}

static void bindValues(QSqlQuery &query, const QVariantList &values)
{
    for (int i = 0; i < values.count(); ++i) {
        query.bindValue(i, values.at(i));
    }
}

static void bindValues(QSqlQuery &query, const QMap<QString, QVariant> &values)
{
    QMap<QString, QVariant>::const_iterator it = values.constBegin(), end = values.constEnd();
    for ( ; it != end; ++it) {
        query.bindValue(it.key(), it.value());
    }
}

static bool countTransientTables(QSqlDatabase &db, const QString &table, int *count)
{
    static const QString sql(QString::fromLatin1("SELECT COUNT(*) FROM sqlite_temp_master WHERE type = 'table' and name LIKE '%1_transient%'"));

    *count = 0;

    QSqlQuery query(db);
    if (!query.exec(sql.arg(table))) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to count transient tables for table: %1").arg(table));
        return false;
    } else while (query.next()) {
        *count = query.value(0).toInt();
    }

    return true;
}

static bool findTransientTables(QSqlDatabase &db, const QString &table, QStringList *tableNames)
{
    static const QString sql(QString::fromLatin1("SELECT name FROM sqlite_temp_master WHERE type = 'table' and name LIKE '%1_transient%'"));

    QSqlQuery query(db);
    if (!query.exec(sql.arg(table))) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to query transient tables for table: %1").arg(table));
        return false;
    } else while (query.next()) {
        tableNames->append(query.value(0).toString());
    }

    return true;
}

static bool dropTransientTables(QSqlDatabase &db, const QString &table)
{
    static const QString dropTableStatement = QString::fromLatin1("DROP TABLE temp.%1");

    QStringList tableNames;
    if (!findTransientTables(db, table, &tableNames))
        return false;

    foreach (const QString tableName, tableNames) {
        QSqlQuery dropTableQuery(db);
        const QString dropStatement(dropTableStatement.arg(tableName));
        if (!dropTableQuery.prepare(dropStatement)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare drop transient table query: %1\n%2")
                    .arg(dropTableQuery.lastError().text())
                    .arg(dropStatement));
            return false;
        }
        if (!dropTableQuery.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to drop transient temporary table: %1\n%2")
                    .arg(dropTableQuery.lastError().text())
                    .arg(dropStatement));
            return false;
        }
    }

    return true;
}

template<typename ValueContainer>
bool createTemporaryContactIdsTable(QSqlDatabase &db, const QString &table, bool filter, const QVariantList &boundIds, 
                                    const QString &join, const QString &where, const QString &orderBy, const ValueContainer &boundValues)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    static const QString createStatement(QString::fromLatin1("CREATE TABLE IF NOT EXISTS temp.%1 (contactId INTEGER)"));
    static const QString insertFilterStatement(QString::fromLatin1("INSERT INTO temp.%1 (contactId) SELECT Contacts.contactId FROM Contacts %2 %3"));

    // Create the temporary table (if we haven't already).
    QSqlQuery tableQuery(db);
    if (!tableQuery.prepare(createStatement.arg(table))) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary table query: %1\n%2")
                .arg(tableQuery.lastError().text())
                .arg(createStatement));
        return false;
    }
    if (!tableQuery.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create temporary table: %1\n%2")
                .arg(tableQuery.lastError().text())
                .arg(createStatement));
        return false;
    }
    tableQuery.finish();

    // insert into the temporary table, all of the ids
    // which will be specified either by id list, or by filter.
    QSqlQuery insertQuery(db);
    if (filter) {
        // specified by filter
        QString insertStatement = insertFilterStatement.arg(table).arg(join).arg(where);
        if (!orderBy.isEmpty()) {
            insertStatement.append(QString::fromLatin1(" ORDER BY ") + orderBy);
        }
        if (!insertQuery.prepare(insertStatement)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary contact ids: %1\n%2")
                    .arg(insertQuery.lastError().text())
                    .arg(insertStatement));
            return false;
        }
        bindValues(insertQuery, boundValues);
        if (!insertQuery.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert temporary contact ids: %1\n%2")
                    .arg(insertQuery.lastError().text())
                    .arg(insertStatement));
            return false;
        } else {
            debugFilterExpansion("Contacts selection:", insertStatement, boundValues);
        }
        insertQuery.finish();
    } else {
        // specified by id list
        // NOTE: we must preserve the order of the bound ids being
        // inserted (to match the order of the input list), so that
        // the result of queryContacts() is ordered according to the
        // order of input ids.
        if (!boundIds.isEmpty()) {
            QVariantList::const_iterator it = boundIds.constBegin(), end = boundIds.constEnd();
            while (it != end) {
                // SQLite allows up to 500 rows per insert
                quint32 remainder = (end - it);
                QVariantList::const_iterator batchEnd = it + std::min<quint32>(remainder, 500);

                QString insertStatement = QString::fromLatin1("INSERT INTO temp.%1 (contactId) VALUES ").arg(table);
                while (true) {
                    const QVariant &v(*it);
                    const quint32 dbId(v.value<quint32>());
                    insertStatement.append(QString::fromLatin1("(%1)").arg(dbId));
                    if (++it == batchEnd) {
                        break;
                    } else {
                        insertStatement.append(QString::fromLatin1(","));
                    }
                }

                if (!insertQuery.prepare(insertStatement)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary contact ids: %1\n%2")
                            .arg(insertQuery.lastError().text())
                            .arg(insertStatement));
                    return false;
                }
                if (!insertQuery.exec()) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert temporary contact ids: %1\n%2")
                            .arg(insertQuery.lastError().text())
                            .arg(insertStatement));
                    return false;
                }
                insertQuery.finish();
            }
        }
    }

    return true;
}

void dropOrDeleteTable(QSqlDatabase &db, const QString &table)
{
    QSqlQuery dropTableQuery(db);
    const QString dropTableStatement = QString::fromLatin1("DROP TABLE IF EXISTS temp.%1").arg(table);
    if (!dropTableQuery.prepare(dropTableStatement) || !dropTableQuery.exec()) {
        // couldn't drop the table, just delete all entries instead.
        QSqlQuery deleteRecordsQuery(db);
        const QString deleteRecordsStatement = QString::fromLatin1("DELETE FROM temp.%1").arg(table);
        if (!deleteRecordsQuery.prepare(deleteRecordsStatement)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare delete records query - the next query may return spurious results: %1\n%2")
                    .arg(deleteRecordsQuery.lastError().text())
                    .arg(deleteRecordsStatement));
        }
        if (!deleteRecordsQuery.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to delete temporary records - the next query may return spurious results: %1\n%2")
                    .arg(deleteRecordsQuery.lastError().text())
                    .arg(deleteRecordsStatement));
        }
    }
}

void clearTemporaryContactIdsTable(QSqlDatabase &db, const QString &table)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    // Drop any transient tables associated with this table
    dropTransientTables(db, table);

    dropOrDeleteTable(db, table);
}

bool createTemporaryValuesTable(QSqlDatabase &db, const QString &table, const QVariantList &values)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    static const QString createStatement(QString::fromLatin1("CREATE TABLE IF NOT EXISTS temp.%1 (value BLOB)"));

    // Create the temporary table (if we haven't already).
    QSqlQuery tableQuery(db);
    if (!tableQuery.prepare(createStatement.arg(table))) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary table query: %1\n%2")
                .arg(tableQuery.lastError().text())
                .arg(createStatement));
        return false;
    }
    if (!tableQuery.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create temporary table: %1\n%2")
                .arg(tableQuery.lastError().text())
                .arg(createStatement));
        return false;
    }
    tableQuery.finish();

    // insert into the temporary table, all of the values
    if (!values.isEmpty()) {
        QSqlQuery insertQuery(db);
        QVariantList::const_iterator it = values.constBegin(), end = values.constEnd();
        while (it != end) {
            // SQLite allows up to 500 rows per insert
            quint32 first = (it - values.constBegin());
            quint32 remainder = (end - it);
            quint32 count = std::min<quint32>(remainder, 500);
            QVariantList::const_iterator batchEnd = it + count;

            QString insertStatement = QString::fromLatin1("INSERT INTO temp.%1 (value) VALUES ").arg(table);
            while (true) {
                insertStatement.append(QString::fromLatin1("(?)"));
                if (++it == batchEnd) {
                    break;
                } else {
                    insertStatement.append(QString::fromLatin1(","));
                }
            }

            if (!insertQuery.prepare(insertStatement)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare temporary values: %1\n%2")
                        .arg(insertQuery.lastError().text())
                        .arg(insertStatement));
                return false;
            }

            foreach (const QVariant &v, values.mid(first, count)) {
                insertQuery.addBindValue(v);
            }

            if (!insertQuery.exec()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert temporary values: %1\n%2")
                        .arg(insertQuery.lastError().text())
                        .arg(insertStatement));
                return false;
            }
            insertQuery.finish();
        }
    }

    return true;
}

void clearTemporaryValuesTable(QSqlDatabase &db, const QString &table)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    dropOrDeleteTable(db, table);
}

static bool createTransientContactIdsTable(QSqlDatabase &db, const QString &table, const QVariantList &ids, QString *transientTableName)
{
    QMutexLocker locker(ContactsDatabase::accessMutex());

    static const QString createTableStatement(QString::fromLatin1("CREATE TABLE %1 (contactId INTEGER)"));
    static const QString insertIdsStatement(QString::fromLatin1("INSERT INTO %1 (contactId) VALUES(:contactId)"));

    int existingTables = 0;
    if (!countTransientTables(db, table, &existingTables))
        return false;

    QString tableName(QString::fromLatin1("temp.%1_transient%2").arg(table).arg(existingTables));

    QSqlQuery tableQuery(db);
    const QString createStatement(createTableStatement.arg(tableName));
    if (!tableQuery.prepare(createStatement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare transient table query: %1\n%2")
                .arg(tableQuery.lastError().text())
                .arg(createStatement));
        return false;
    }
    if (!tableQuery.exec()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create transient table: %1\n%2")
                .arg(tableQuery.lastError().text())
                .arg(createStatement));
        return false;
    }
    tableQuery.finish();

    QSqlQuery insertQuery(db);

    QVariantList::const_iterator it = ids.constBegin(), end = ids.constEnd();
    while (it != end) {
        // SQLite allows up to 500 rows per insert
        quint32 remainder = (end - it);
        QVariantList::const_iterator batchEnd = it + std::min<quint32>(remainder, 500);

        QString insertStatement = QString::fromLatin1("INSERT INTO %1 (contactId) VALUES ").arg(tableName);
        while (true) {
            const QVariant &v(*it);
            const quint32 dbId(v.value<quint32>());
            insertStatement.append(QString::fromLatin1("(%1)").arg(dbId));
            if (++it == batchEnd) {
                break;
            } else {
                insertStatement.append(QString::fromLatin1(","));
            }
        }

        if (!insertQuery.prepare(insertStatement)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare transient contact ids: %1\n%2")
                    .arg(insertQuery.lastError().text())
                    .arg(insertStatement));
            return false;
        }
        if (!insertQuery.exec()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert transient contact ids: %1\n%2")
                    .arg(insertQuery.lastError().text())
                    .arg(insertStatement));
            return false;
        }
        insertQuery.finish();
    }

    *transientTableName = tableName;
    return true;
}

// QDir::isReadable() doesn't support group permissions, only user permissions.
bool directoryIsRW(const QString &dirPath)
{
  QFileInfo databaseDirInfo(dirPath);
  return (databaseDirInfo.permission(QFile::ReadGroup | QFile::WriteGroup)
       || databaseDirInfo.permission(QFile::ReadUser  | QFile::WriteUser));
}

QSqlDatabase ContactsDatabase::open(const QString &databaseName, bool nonprivileged)
{
    QMutexLocker locker(accessMutex());

    // horrible hack: Qt4 didn't have GenericDataLocation so we hardcode DATA_DIR location.
    QString privilegedDataDir(QString("%1/%2/")
            .arg(QString::fromLatin1(QTCONTACTS_SQLITE_CENTRAL_DATA_DIR))
            .arg(QString::fromLatin1(QTCONTACTS_SQLITE_PRIVILEGED_DIR)));
    QString unprivilegedDataDir(QString::fromLatin1(QTCONTACTS_SQLITE_CENTRAL_DATA_DIR));

    QDir databaseDir;
    if (!nonprivileged && databaseDir.mkpath(privilegedDataDir + QString::fromLatin1(QTCONTACTS_SQLITE_DATABASE_DIR))) {
        // privileged.
        databaseDir = privilegedDataDir + QString::fromLatin1(QTCONTACTS_SQLITE_DATABASE_DIR);
    } else {
        // not privileged.
        if (!databaseDir.mkpath(unprivilegedDataDir + QString::fromLatin1(QTCONTACTS_SQLITE_DATABASE_DIR))) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to create contacts database directory: %1").arg(unprivilegedDataDir + QString::fromLatin1(QTCONTACTS_SQLITE_DATABASE_DIR)));
            return QSqlDatabase();
        }
        databaseDir = unprivilegedDataDir + QString::fromLatin1(QTCONTACTS_SQLITE_DATABASE_DIR);
    }

    const QString databaseFile = databaseDir.absoluteFilePath(QString::fromLatin1(QTCONTACTS_SQLITE_DATABASE_NAME));
    const bool exists = QFile::exists(databaseFile);

    QSqlDatabase database = QSqlDatabase::addDatabase(QString::fromLatin1("QSQLITE"), databaseName);
    database.setDatabaseName(databaseFile);

    if (!database.open()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to open contacts database: %1")
                .arg(database.lastError().text()));
        return database;
    }

    if (!exists && !prepareDatabase(database)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare contacts database - removing: %1")
                .arg(database.lastError().text()));

        database.close();
        QFile::remove(databaseFile);

        return database;
    } else {
        if (!upgradeDatabase(database)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to upgrade contacts database: %1")
                    .arg(database.lastError().text()));
            return database;
        }

        if (!configureDatabase(database)) {
            return database;
        }
    }

    QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Opened contacts database: %1").arg(databaseFile));
    return database;
}

bool ContactsDatabase::beginTransaction(QSqlDatabase &database)
{
    return ::beginTransaction(database);
}

bool ContactsDatabase::commitTransaction(QSqlDatabase &database)
{
    return ::commitTransaction(database);
}

bool ContactsDatabase::rollbackTransaction(QSqlDatabase &database)
{
    return ::rollbackTransaction(database);
}

QSqlQuery ContactsDatabase::prepare(const char *statement, const QSqlDatabase &database)
{
    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare query: %1\n%2")
                .arg(query.lastError().text())
                .arg(statement));
        return QSqlQuery();
    }
    return query;
}

QString ContactsDatabase::expandQuery(const QString &queryString, const QVariantList &bindings)
{
    QString query(queryString);

    int index = 0;
    for (int i = 0; i < bindings.count(); ++i) {
        static const QChar marker = QChar::fromLatin1('?');

        QString value = bindings.at(i).toString();
        index = query.indexOf(marker, index);
        if (index == -1)
            break;

        query.replace(index, 1, value);
        index += value.length();
    }

    return query;
}

QString ContactsDatabase::expandQuery(const QString &queryString, const QMap<QString, QVariant> &bindings)
{
    QString query(queryString);

    int index = 0;

    while (true) {
        static const QChar marker = QChar::fromLatin1(':');

        index = query.indexOf(marker, index);
        if (index == -1)
            break;

        int remaining = query.length() - index;
        int len = 1;
        for ( ; (len < remaining) && query.at(index + len).isLetter(); ) {
            ++len;
        }

        const QString key(query.mid(index, len));
        QVariant value = bindings.value(key);

        QString valueText;
        if (value.type() == QVariant::String) {
            valueText = QString::fromLatin1("'%1'").arg(value.toString());
        } else {
            valueText = value.toString();
        }

        query.replace(index, len, valueText);
        index += valueText.length();
    }

    return query;
}

QString ContactsDatabase::expandQuery(const QSqlQuery &query)
{
    return expandQuery(query.lastQuery(), query.boundValues());
}

bool ContactsDatabase::createTemporaryContactIdsTable(QSqlDatabase &db, const QString &table, const QVariantList &boundIds)
{
    return ::createTemporaryContactIdsTable(db, table, false, boundIds, QString(), QString(), QString(), QVariantList());
}

bool ContactsDatabase::createTemporaryContactIdsTable(QSqlDatabase &db, const QString &table, const QString &join, const QString &where, const QString &orderBy, const QVariantList &boundValues)
{
    return ::createTemporaryContactIdsTable(db, table, true, QVariantList(), join, where, orderBy, boundValues);
}

bool ContactsDatabase::createTemporaryContactIdsTable(QSqlDatabase &db, const QString &table, const QString &join, const QString &where, const QString &orderBy, const QMap<QString, QVariant> &boundValues)
{
    return ::createTemporaryContactIdsTable(db, table, true, QVariantList(), join, where, orderBy, boundValues);
}

void ContactsDatabase::clearTemporaryContactIdsTable(QSqlDatabase &db, const QString &table)
{
    ::clearTemporaryContactIdsTable(db, table);
}

bool ContactsDatabase::createTemporaryValuesTable(QSqlDatabase &db, const QString &table, const QVariantList &values)
{
    return ::createTemporaryValuesTable(db, table, values);
}

void ContactsDatabase::clearTemporaryValuesTable(QSqlDatabase &db, const QString &table)
{
    ::clearTemporaryValuesTable(db, table);
}

bool ContactsDatabase::createTransientContactIdsTable(QSqlDatabase &db, const QString &table, const QVariantList &ids, QString *transientTableName)
{
    return ::createTransientContactIdsTable(db, table, ids, transientTableName);
}

void ContactsDatabase::clearTransientContactIdsTable(QSqlDatabase &db, const QString &table)
{
    ::dropTransientTables(db, table);
}

QMutex *ContactsDatabase::accessMutex()
{
    return databaseMutex();
}

#include "../extensions/qcontactdeactivated_impl.h"
#include "../extensions/qcontactincidental_impl.h"
#include "../extensions/qcontactoriginmetadata_impl.h"
#include "../extensions/qcontactstatusflags_impl.h"
