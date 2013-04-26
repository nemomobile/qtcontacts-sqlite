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

#ifndef QTCONTACTSSQLITE_CONTACTSENGINE
#define QTCONTACTSSQLITE_CONTACTSENGINE

#include <QSqlDatabase>

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#include <QContactManagerEngineV2>
#else
#include <QContactManagerEngine>
#include <QContactType>
#endif

#include "contactidimpl.h"
#include "contactreader.h"
#include "contactwriter.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
QTM_USE_NAMESPACE
#else
QTCONTACTS_USE_NAMESPACE
#endif

class JobThread;

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
class ContactsEngine : public QContactManagerEngineV2
#else
class ContactsEngine : public QContactManagerEngine
#endif
{
    Q_OBJECT
public:
    ContactsEngine(const QString &name);
    ~ContactsEngine();

    QContactManager::Error open();

    QString managerName() const;
    int managerVersion() const;

    QList<QContactIdClassName> contactIds(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                QContactManager::Error* error) const;
    QList<QContact> contacts(
                const QList<QContactIdClassName> &localIds,
                const QContactFetchHint &fetchHint,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) const;
    QContact contact(
            const QContactIdClassName &contactId,
            const QContactFetchHint &fetchHint,
            QContactManager::Error* error) const;

    QList<QContact> contacts(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                const QContactFetchHint &fetchHint,
                QContactManager::Error* error) const;
    QList<QContact> contacts(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                const QContactFetchHint &fetchHint,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) const;
    bool saveContacts(
                QList<QContact> *contacts,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error);
    bool saveContacts(
                QList<QContact> *contacts,
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
                const QStringList &definitionMask,
#else
                const QList<QContactDetail::DetailType> &typeMask,
#endif
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error);
    bool removeContact(const QContactIdClassName& contactId, QContactManager::Error* error);
    bool removeContacts(
                const QList<QContactIdClassName> &contactIds,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error* error);

    QContactIdClassName selfContactId(QContactManager::Error* error) const;
    bool setSelfContactId(const QContactIdClassName& contactId, QContactManager::Error* error);

    QList<QContactRelationship> relationships(
            const QString &relationshipType,
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
            const QContactId &participantId,
#else
            const QContact &participant,
#endif
            QContactRelationship::Role role,
            QContactManager::Error *error) const;
    bool saveRelationships(
            QList<QContactRelationship> *relationships,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error);
    bool removeRelationships(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error);

    void requestDestroyed(QContactAbstractRequest* req);
    bool startRequest(QContactAbstractRequest* req);
    bool cancelRequest(QContactAbstractRequest* req);
    bool waitForRequestFinished(QContactAbstractRequest* req, int msecs);

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
    QMap<QString, QContactDetailDefinition> detailDefinitions(const QString& contactType, QContactManager::Error* error) const;
    bool hasFeature(QContactManager::ManagerFeature feature, const QString& contactType) const;
    bool isRelationshipTypeSupported(const QString& relationshipType, const QString& contactType) const;
    QStringList supportedContactTypes() const;
#else
    bool isRelationshipTypeSupported(const QString& relationshipType, const QString& contactType) const;
    QList<QContactType::TypeValues> supportedContactTypes() const;
#endif

private slots:
    void _q_contactsChanged(const QList<QContactIdClassName> &contacts);
    void _q_contactsAdded(const QList<QContactIdClassName> &contacts);
    void _q_contactsRemoved(const QList<QContactIdClassName> &contacts);
    void _q_selfContactIdChanged(quint32,quint32);

private:
    QString databaseUuid();
    QString m_databaseUuid;
    const QString m_name;
    QSqlDatabase m_database;
    mutable ContactReader *m_synchronousReader;
    ContactWriter *m_synchronousWriter;
    JobThread *m_jobThread;
};


#endif

