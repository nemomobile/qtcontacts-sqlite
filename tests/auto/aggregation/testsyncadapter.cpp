/*
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jolla.com>
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

#include "testsyncadapter.h"
#include "../../../src/extensions/twowaycontactsyncadapter_impl.h"

#include <QTimer>

#include <QContact>
#include <QContactPhoneNumber>
#include <QContactEmailAddress>
#include <QContactName>

#define TSA_GUID_STRING(accountId, fname, lname) QString(accountId + ":" + fname + lname)

TestSyncAdapter::TestSyncAdapter(QObject *parent)
    : QObject(parent), TwoWayContactSyncAdapter(QStringLiteral("testsyncadapter"))
{
}

TestSyncAdapter::~TestSyncAdapter()
{
}

void TestSyncAdapter::addRemoteContact(const QString &accountId, const QString &fname, const QString &lname, const QString &phone)
{
    QString contactGuidStr(TSA_GUID_STRING(accountId, fname, lname));
    QContact newContact;

    QContactName ncn;
    ncn.setFirstName(fname);
    ncn.setLastName(lname);
    QContactPhoneNumber ncp;
    ncp.setNumber(phone);
    QContactGuid nguid;
    nguid.setGuid(contactGuidStr);

    newContact.saveDetail(&ncn);
    newContact.saveDetail(&ncp);
    newContact.saveDetail(&nguid);

    m_remoteServerContacts[accountId].insert(contactGuidStr, newContact);
    m_remoteAddMods[accountId].insert(contactGuidStr);
}

void TestSyncAdapter::removeRemoteContact(const QString &accountId, const QString &fname, const QString &lname)
{
    QString contactGuidStr(TSA_GUID_STRING(accountId, fname, lname));
    QContact remContact = m_remoteServerContacts[accountId][contactGuidStr];

    // stop tracking the contact if we are currently tracking it.
    m_remoteAddMods[accountId].remove(contactGuidStr);

    // remote it from our remote cache
    m_remoteServerContacts[accountId].remove(contactGuidStr);

    // detect whether we need to delete it on the local device
    // we need to delete it if it has a synctarget constituent (ie, has a guid)
    if (!remContact.detail<QContactGuid>().guid().isEmpty()) {
        m_remoteDeletions[accountId].append(remContact);
    }
}

void TestSyncAdapter::setRemoteContact(const QString &accountId, const QString &fname, const QString &lname, const QContact &contact)
{
    QString contactGuidStr(TSA_GUID_STRING(accountId, fname, lname));
    QContact setContact = contact;

    QContactGuid sguid = setContact.detail<QContactGuid>();
    sguid.setGuid(contactGuidStr);
    setContact.saveDetail(&sguid);

    QContactOriginMetadata somd = setContact.detail<QContactOriginMetadata>();
    somd.setGroupId(setContact.id().toString());
    setContact.saveDetail(&somd);

    m_remoteServerContacts[accountId].insert(contactGuidStr, setContact);
    m_remoteAddMods[accountId].insert(contactGuidStr);
}

void TestSyncAdapter::changeRemoteContactPhone(const QString &accountId,  const QString &fname, const QString &lname, const QString &modPhone)
{
    QString contactGuidStr(TSA_GUID_STRING(accountId, fname, lname));
    if (!m_remoteServerContacts[accountId].contains(contactGuidStr)) {
        qWarning() << "Contact:" << contactGuidStr << "doesn't exist remotely!";
        return;
    }

    QContact modContact = m_remoteServerContacts[accountId][contactGuidStr];
    QContactPhoneNumber mcp = modContact.detail<QContactPhoneNumber>();
    mcp.setNumber(modPhone);
    modContact.saveDetail(&mcp);

    m_remoteServerContacts[accountId].insert(contactGuidStr, modContact);
    m_remoteAddMods[accountId].insert(contactGuidStr);
}

void TestSyncAdapter::changeRemoteContactEmail(const QString &accountId,  const QString &fname, const QString &lname, const QString &modEmail)
{
    QString contactGuidStr(TSA_GUID_STRING(accountId, fname, lname));
    if (!m_remoteServerContacts[accountId].contains(contactGuidStr)) {
        qWarning() << "Contact:" << contactGuidStr << "doesn't exist remotely!";
        return;
    }

    QContact modContact = m_remoteServerContacts[accountId][contactGuidStr];
    QContactEmailAddress mce = modContact.detail<QContactEmailAddress>();
    mce.setEmailAddress(modEmail);
    modContact.saveDetail(&mce);

    m_remoteServerContacts[accountId].insert(contactGuidStr, modContact);
    m_remoteAddMods[accountId].insert(contactGuidStr);
}

void TestSyncAdapter::performTwoWaySync(const QString &accountId)
{
    // reset our state.
    m_downsyncWasRequired[accountId] = false;
    m_upsyncWasRequired[accountId] = false;

    // do the sync process as described in twowaycontactsyncadapter.h
    if (!initSyncAdapter(accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't init adapter";
        emit failed();
        return;
    }

    if (!readSyncStateData(&m_remoteSince[accountId], accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't read sync state data";
        emit failed();
        return;
    }

    determineRemoteChanges(m_remoteSince[accountId], accountId);
    // continued in continueTwoWaySync().
}

void TestSyncAdapter::determineRemoteChanges(const QDateTime &, const QString &accountId)
{
    QTimer *simtimer = 0;
    if (!m_simulationTimers.contains(accountId)) {
        simtimer = new QTimer(this);
        simtimer->setSingleShot(true);
        simtimer->setInterval(1100);
        simtimer->setProperty("accountId", accountId);
        m_simulationTimers.insert(accountId, simtimer);
    } else {
        simtimer = m_simulationTimers.value(accountId);
    }

    connect(simtimer, SIGNAL(timeout()), this, SLOT(continueTwoWaySync()));
    simtimer->start();
}

void TestSyncAdapter::continueTwoWaySync()
{
    QTimer *simtimer = qobject_cast<QTimer*>(sender());
    simtimer->disconnect(this, SLOT(continueTwoWaySync()));
    QString accountId = simtimer->property("accountId").toString();

    // continuing the sync process as described in twowaycontactsyncadapter.h
    if (m_remoteDeletions[accountId].isEmpty() && m_remoteAddMods[accountId].isEmpty()) {
        m_downsyncWasRequired[accountId] = false;
    } else {
        m_downsyncWasRequired[accountId] = true;
    }

    // callStoreRemoteChanges anyway so that the state machine continues to work.
    // alternatively, we could set the state to StoredRemoteChanges manually, and skip
    // this call in the else block above, but we should test that it works properly anyway.
    QList<QContact> remoteAddMods;
    foreach (const QString &contactGuidStr, m_remoteAddMods[accountId]) {
        remoteAddMods.append(m_remoteServerContacts[accountId].value(contactGuidStr));
    }

    if (!storeRemoteChanges(m_remoteDeletions[accountId], remoteAddMods, accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't store remote changes";
        emit failed();
        return;
    }

    // clear our simulated remote changes deltas, as we've already reported / stored them.
    m_remoteDeletions[accountId].clear();
    m_remoteAddMods[accountId].clear();

    QList<QContact> locallyAdded, locallyModified, locallyDeleted;
    QDateTime localSince;
    if (!determineLocalChanges(&localSince, &locallyAdded, &locallyModified, &locallyDeleted, accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't determine local changes";
        emit failed();
        return;
    }

    if (locallyAdded.isEmpty() && locallyModified.isEmpty() && locallyDeleted.isEmpty()) {
        m_upsyncWasRequired[accountId] = false;
    } else {
        m_upsyncWasRequired[accountId] = true;
    }

    upsyncLocalChanges(localSince, locallyAdded, locallyModified, locallyDeleted, accountId);
    // continued in finalizeTwoWaySync()
}

void TestSyncAdapter::upsyncLocalChanges(const QDateTime &,
                                         const QList<QContact> &locallyAdded,
                                         const QList<QContact> &locallyModified,
                                         const QList<QContact> &locallyDeleted,
                                         const QString &accountId)
{
    // first, apply the local changes to our in memory store.
    foreach (const QContact &c, locallyAdded + locallyModified) {
        setRemoteContact(accountId, c.detail<QContactName>().firstName(), c.detail<QContactName>().lastName(), c);
    }
    foreach (const QContact &c, locallyDeleted) {
        removeRemoteContact(accountId, c.detail<QContactName>().firstName(), c.detail<QContactName>().lastName());
    }

    // then trigger finalize after a simulated network delay.
    QTimer *simtimer = 0;
    if (!m_simulationTimers.contains(accountId)) {
        simtimer = new QTimer(this);
        simtimer->setSingleShot(true);
        simtimer->setInterval(1100);
        simtimer->setProperty("accountId", accountId);
        m_simulationTimers.insert(accountId, simtimer);
    } else {
        simtimer = m_simulationTimers.value(accountId);
    }

    connect(simtimer, SIGNAL(timeout()), this, SLOT(finalizeTwoWaySync()));
    simtimer->start();
}

void TestSyncAdapter::finalizeTwoWaySync()
{
    QTimer *simtimer = qobject_cast<QTimer*>(sender());
    simtimer->disconnect(this, SLOT(finalizeTwoWaySync()));
    QString accountId = simtimer->property("accountId").toString();

    if (!storeSyncStateData(accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't store sync state data";
        emit failed();
        return;
    }
    emit finished(); // succeeded.
}

bool TestSyncAdapter::upsyncWasRequired(const QString &accountId) const
{
    return m_upsyncWasRequired[accountId];
}

bool TestSyncAdapter::downsyncWasRequired(const QString &accountId) const
{
    return m_downsyncWasRequired[accountId];
}

QContact TestSyncAdapter::remoteContact(const QString &accountId, const QString &fname, const QString &lname) const
{
    return m_remoteServerContacts[accountId][TSA_GUID_STRING(accountId, fname, lname)];
}

