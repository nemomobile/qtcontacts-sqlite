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

#ifdef USING_QTPIM
#include <QContactManagerEngine>
#else
#include <QContactManagerEngineV2>
#endif

#include <QSqlDatabase>

#include "contactreader.h"
#include "contactwriter.h"
#include "contactid_p.h"

#ifdef USING_QTPIM
// QList<int> is widely used in qtpim
Q_DECLARE_METATYPE(QList<int>)
#endif

USE_CONTACTS_NAMESPACE

// Force an ambiguity with QContactDetail::operator== so that we can't call it
// It does not compare correctly if the values contains QList<int>
inline void operator==(const QContactDetail &, const QContactDetail &) {}

class JobThread;

class ContactsEngine
#ifdef USING_QTPIM
    : public QContactManagerEngine
#else
    : public QContactManagerEngineV2
#endif
{
    Q_OBJECT
public:
    ContactsEngine(const QString &name);
    ~ContactsEngine();

    QContactManager::Error open();

    QString managerName() const;
    int managerVersion() const;

    QList<QContactIdType> contactIds(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                QContactManager::Error* error) const;
    QList<QContact> contacts(
                const QList<QContactIdType> &localIds,
                const QContactFetchHint &fetchHint,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) const;
    QContact contact(
            const QContactIdType &contactId,
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
                const ContactWriter::DetailList &definitionMask,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error);
    bool removeContact(const QContactIdType& contactId, QContactManager::Error* error);
    bool removeContacts(
                const QList<QContactIdType> &contactIds,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error* error);

    QContactIdType selfContactId(QContactManager::Error* error) const;
    bool setSelfContactId(const QContactIdType& contactId, QContactManager::Error* error);

    QList<QContactRelationship> relationships(
            const QString &relationshipType,
#ifdef USING_QTPIM
            const QContact &participant,
#else
            const QContactId &participantId,
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

#ifndef USING_QTPIM
    QMap<QString, QContactDetailDefinition> detailDefinitions(const QString& contactType, QContactManager::Error* error) const;
    bool hasFeature(QContactManager::ManagerFeature feature, const QString& contactType) const;
#endif

#ifdef USING_QTPIM
    bool isRelationshipTypeSupported(const QString &relationshipType, QContactType::TypeValues contactType) const;
    QList<QContactType::TypeValues> supportedContactTypes() const;
#else
    bool isRelationshipTypeSupported(const QString& relationshipType, const QString& contactType) const;
    QStringList supportedContactTypes() const;
#endif

    void regenerateDisplayLabel(QContact &contact) const;

#ifdef USING_QTPIM
    static bool setContactDisplayLabel(QContact *contact, const QString &label);
#endif

    static QString normalizedPhoneNumber(const QString &input);

#ifndef USING_QTPIM
    virtual
#endif
    QString synthesizedDisplayLabel(const QContact &contact, QContactManager::Error *error) const;

private slots:
    void _q_contactsChanged(const QVector<quint32> &contactIds);
    void _q_contactsAdded(const QVector<quint32> &contactIds);
    void _q_contactsRemoved(const QVector<quint32> &contactIds);
    void _q_selfContactIdChanged(quint32,quint32);
    void _q_relationshipsAdded(const QVector<quint32> &contactIds);
    void _q_relationshipsRemoved(const QVector<quint32> &contactIds);

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

