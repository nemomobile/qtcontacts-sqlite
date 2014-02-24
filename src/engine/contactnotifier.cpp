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

#include "contactnotifier.h"
#include "trace_p.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QVector>

#include <QDebug>

#define NOTIFIER_PATH "/org/nemomobile/contacts/sqlite"
#define NOTIFIER_INTERFACE "org.nemomobile.contacts.sqlite"

Q_DECLARE_METATYPE(QVector<quint32>)

namespace ContactNotifier
{

void initialize()
{
    qDBusRegisterMetaType<QVector<quint32> >();
}

QVector<quint32> idVector(const QList<QContactId> &contactIds)
{
    QVector<quint32> ids;
    ids.reserve(contactIds.size());
    foreach (const QContactId &id, contactIds) {
        ids.append(ContactId::databaseId(id));
    }
    return ids;
}

void contactsAdded(const QList<QContactId> &contactIds)
{
    if (!contactIds.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("contactsAdded"));
        message.setArguments(QVariantList() << QVariant::fromValue(idVector(contactIds)));
        QDBusConnection::sessionBus().send(message);
    }
}

void contactsChanged(const QList<QContactId> &contactIds)
{
    if (!contactIds.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("contactsChanged"));
        message.setArguments(QVariantList() << QVariant::fromValue(idVector(contactIds)));
        QDBusConnection::sessionBus().send(message);
    }
}

void contactsPresenceChanged(const QList<QContactId> &contactIds)
{
    if (!contactIds.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("contactsPresenceChanged"));
        message.setArguments(QVariantList() << QVariant::fromValue(idVector(contactIds)));
        QDBusConnection::sessionBus().send(message);
    }
}

void syncContactsChanged(const QStringList &syncTargets)
{
    if (!syncTargets.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("syncContactsChanged"));
        message.setArguments(QVariantList() << syncTargets);
        QDBusConnection::sessionBus().send(message);
    }
}

void contactsRemoved(const QList<QContactId> &contactIds)
{
    if (!contactIds.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("contactsRemoved"));
        message.setArguments(QVariantList() << QVariant::fromValue(idVector(contactIds)));
        QDBusConnection::sessionBus().send(message);
    }
}

void selfContactIdChanged(QContactId oldId, QContactId newId)
{
    if (oldId != newId) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("selfContactIdChanged"));
        message.setArguments(QVariantList() << QVariant::fromValue(ContactId::databaseId(oldId)) << QVariant::fromValue(ContactId::databaseId(newId)));
        QDBusConnection::sessionBus().send(message);
    }
}

void relationshipsAdded(const QSet<QContactId> &contactIds)
{
    if (!contactIds.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("relationshipsAdded"));
        message.setArguments(QVariantList() << QVariant::fromValue(idVector(contactIds.toList())));
        QDBusConnection::sessionBus().send(message);
    }
}

void relationshipsRemoved(const QSet<QContactId> &contactIds)
{
    if (!contactIds.isEmpty()) {
        QDBusMessage message = QDBusMessage::createSignal(
                    QLatin1String(NOTIFIER_PATH),
                    QLatin1String(NOTIFIER_INTERFACE),
                    QLatin1String("relationshipsRemoved"));
        message.setArguments(QVariantList() << QVariant::fromValue(idVector(contactIds.toList())));
        QDBusConnection::sessionBus().send(message);
    }
}

bool connect(const char *name, const char *signature, QObject *receiver, const char *slot)
{
    static QDBusConnection connection(QDBusConnection::sessionBus());

    if (!connection.isConnected()) {
        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Session Bus is not connected"));
        return false;
    }

    if (!connection.connect(QString(),
                            QLatin1String(NOTIFIER_PATH),
                            QLatin1String(NOTIFIER_INTERFACE),
                            QLatin1String(name),
                            QLatin1String(signature),
                            receiver,
                            slot)) {
        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Unable to connect DBUS signal: %1").arg(name));
        return false;
    }

    return true;
}

}
