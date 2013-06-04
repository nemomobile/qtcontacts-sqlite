/*
 * Copyright (C) 2013 Jolla Ltd. <chris.adams@jollamobile.com>
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

#ifndef QTCONTACTSSQLITE_CONTACTID_P_H
#define QTCONTACTSSQLITE_CONTACTID_P_H

#include <QtGlobal>
#include <QContactId>
#include <QContact>

#ifdef USING_QTPIM

#include <QContactEngineId>

#define QContactIdType QContactId

USE_CONTACTS_NAMESPACE

class ContactId : public QContactEngineId
{
public:
    static QContactId apiId(const QContact &contact);
    static QContactId apiId(quint32 databaseId);

    static quint32 databaseId(const QContact &contact);
    static quint32 databaseId(const QContactId &apiId);

    static const QContactId &contactId(const QContactId &apiId);

    static bool isValid(const QContact &contact);
    static bool isValid(const QContactId &apiId);
    static bool isValid(quint32 dbId);

    static QString toString(const QContactIdType &apiId);
    static QString toString(const QContact &c);

    static QContactIdType fromString(const QString &id);

    ContactId(quint32 databaseId);

    // implementing QContactEngineId:
    bool isEqualTo(const QContactEngineId *other) const;
    bool isLessThan(const QContactEngineId *other) const;
    QString managerUri() const;
    QContactEngineId* clone() const;
    QString toString() const;

#ifndef QT_NO_DEBUG_STREAM
    QDebug& debugStreamOut(QDebug &dbg) const;
#endif

    uint hash() const;

private:
    quint32 m_databaseId;
};

#else // USING_QTPIM

#define QContactIdType QContactLocalId

USE_CONTACTS_NAMESPACE

class ContactId
{
public:
    static QContactLocalId apiId(const QContact &contact);
    static QContactLocalId apiId(quint32 databaseId);

    static quint32 databaseId(const QContact &contact);
    static quint32 databaseId(const QContactId &contactId);
    static quint32 databaseId(QContactLocalId apiId);

    static QContactId contactId(QContactLocalId apiId);

    static bool isValid(const QContact &contact);
    static bool isValid(const QContactId &contactId);
    static bool isValid(quint32 dbId);

    static QString toString(const QContactIdType &apiId);
    static QString toString(const QContact &c);
};

#endif // USING_QTPIM

#endif // QTCONTACTSSQLITE_CONTACTIDIMPL

