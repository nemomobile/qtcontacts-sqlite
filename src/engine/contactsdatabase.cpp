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

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include <QtDebug>

USE_CONTACTS_NAMESPACE

static const char *setupEncoding =
        "\n PRAGMA encoding = \"UTF-16\";";

static const char *setupTempStore =
        "\n PRAGMA temp_store = MEMORY;";

static const char *setupJournal =
        "\n PRAGMA journal_mode = WAL;";

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
        "\n isFavorite BOOL);";

static const char *createAddressesTable =
        "\n CREATE TABLE Addresses ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY ASC,"
        "\n street TEXT,"
        "\n postOfficeBox TEXT,"
        "\n region TEXT,"
        "\n locality TEXT,"
        "\n postCode TEXT,"
        "\n country TEXT);";

static const char *createAnniversariesTable =
        "\n CREATE TABLE Anniversaries ("
        "\n detailId INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
        "\n contactId INTEGER KEY,"
        "\n originalDateTime DATETIME,"
        "\n calendarId TEXT,"
        "\n subType INTEGER);";

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
        "\n enabled BOOL);";

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

static const char *createDetailsTable =
        "\n CREATE TABLE Details ("
        "\n contactId INTEGER KEY,"
        "\n detailId INTEGER,"
        "\n detail TEXT,"
        "\n detailUri TEXT UNIQUE,"
        "\n linkedDetailUris TEXT,"
        "\n contexts TEXT,"
        "\n accessConstraints INTEGER);";

static const char *createDetailsJoinIndex =
        "\n CREATE INDEX DetailsJoinIndex ON Details(detailId, detail);";

static const char *createDetailsRemoveIndex =
        "\n CREATE INDEX DetailsRemoveIndex ON Details(contactId, detail);";

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

static const char *createRemoveTrigger =
        "\n CREATE TRIGGER RemoveContactDetails"
        "\n BEFORE DELETE"
        "\n ON Contacts"
        "\n BEGIN"
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

static const char *createContactsIndex =
        "\n CREATE INDEX ContactsIndex ON Contacts(syncTarget, lowerFirstName, lowerLastName);";

static const char *createRelationshipsIndex =
        "\n CREATE INDEX RelationshipsIndex ON Relationships(firstId, secondId);";

static const char *createPhoneNumbersIndex =
        "\n CREATE INDEX PhoneNumbersIndex ON PhoneNumbers(normalizedNumber);";

static const char *createEmailAddressesIndex =
        "\n CREATE INDEX EmailAddressesIndex ON EmailAddresses(lowerEmailAddress);";

static const char *createOnlineAccountsIndex =
        "\n CREATE INDEX OnlineAccountsIndex ON OnlineAccounts(lowerAccountUri);";

static const char *createNicknamesIndex =
        "\n CREATE INDEX NicknamesIndex ON Nicknames(lowerNickname);";

static const char *createTpMetadataIndex =
        "\n CREATE INDEX TpMetadataIndex ON TpMetadata(telepathyId, accountId);";

static const char *createTables[] =
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
    createDetailsTable,
    createDetailsJoinIndex,
    createDetailsRemoveIndex,
    createIdentitiesTable,
    createRelationshipsTable,
    createRemoveTrigger,
    createLocalSelfContact,
#ifdef QTCONTACTS_SQLITE_PERFORM_AGGREGATION
    createAggregateSelfContact,
    createSelfContactRelationship,
#endif
    createContactsIndex,
    createRelationshipsIndex,
    createPhoneNumbersIndex,
    createEmailAddressesIndex,
    createOnlineAccountsIndex,
    createNicknamesIndex,
    createTpMetadataIndex
};

template <typename T, int N> static int lengthOf(const T(&)[N]) { return N; }

static bool execute(QSqlDatabase &database, const QString &statement)
{
    QSqlQuery query(database);
    if (!query.exec(statement)) {
        qWarning() << "Query failed";
        qWarning() << query.lastError();
        qWarning() << statement;
        return false;
    } else {
        return true;
    }
}

static bool prepareDatabase(QSqlDatabase &database)
{
    if (!execute(database, QLatin1String(setupEncoding))
            || !execute(database, QLatin1String(setupTempStore))
            || !execute(database, QLatin1String(setupJournal))) {
        return false;
    }

    if (!database.transaction())
        return false;

    bool error = false;
    for (int i = 0; i < lengthOf(createTables); ++i) {
        QSqlQuery query(database);

        if (!query.exec(QLatin1String(createTables[i]))) {
            qWarning() << "Table creation failed";
            qWarning() << query.lastError();
            qWarning() << createTables[i];
            error = true;
            break;
        }
    }
    if (error) {
        database.rollback();
        return false;
    } else {
        return database.commit();
    }
}

QSqlDatabase ContactsDatabase::open(const QString &databaseName)
{
    // horrible hack: Qt4 didn't have GenericDataLocation so we hardcode database location.
    QDir databaseDir(QLatin1String(QTCONTACTS_SQLITE_DATABASE_DIR));
    if (!databaseDir.exists()) {
        databaseDir.mkpath(QLatin1String("."));
    }

    const QString databaseFile = databaseDir.absoluteFilePath(QLatin1String(QTCONTACTS_SQLITE_DATABASE_NAME));
    const bool exists = QFile::exists(databaseFile);

    QSqlDatabase database = QSqlDatabase::addDatabase(QLatin1String("QSQLITE"), databaseName);
    database.setDatabaseName(databaseFile);

    if (!database.open()) {
        qWarning() << "Failed to open contacts database";
        qWarning() << database.lastError();
        return database;
    } else {
        qWarning() << "Opened contacts database:" << databaseFile;
    }

    if (!exists && !prepareDatabase(database)) {
        database.close();

        QFile::remove(databaseFile);

        return database;
    } else {
        database.exec(QLatin1String(setupTempStore));
        database.exec(QLatin1String(setupJournal));
    }

    return database;
}

QSqlQuery ContactsDatabase::prepare(const char *statement, const QSqlDatabase &database)
{
    QSqlQuery query(database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        qWarning() << "Failed to prepare query";
        qWarning() << query.lastError();
        qWarning() << statement;
        return QSqlQuery();
    }
    return query;
}

#ifdef USING_QTPIM
const QContactDetail::DetailType QContactTpMetadata::Type(static_cast<QContactDetail::DetailType>(QContactDetail::TypeVersion + 1));
#else
Q_IMPLEMENT_CUSTOM_CONTACT_DETAIL(QContactTpMetadata, "TpMetadata");
#endif
