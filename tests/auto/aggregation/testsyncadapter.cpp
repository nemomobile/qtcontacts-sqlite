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
#include "twowaycontactsyncadapter_impl.h"

#include <QTimer>

TestSyncAdapter::TestSyncAdapter(QObject *parent)
    : QObject(parent), TwoWayContactSyncAdapter(QStringLiteral("testsyncadapter"))
{
}

TestSyncAdapter::~TestSyncAdapter()
{
}

void TestSyncAdapter::setRemoteDelta(const QString &accountId,
                                     const QList<QContact> &deletions,
                                     const QList<QContact> &addmods)
{
    m_remoteDelta.insert(accountId, QPair<QList<QContact>, QList<QContact> >(deletions, addmods));
}

void TestSyncAdapter::performTwoWaySync(const QString &accountId)
{
    // clear our state
    m_accountId = accountId;
    m_remoteSince = QDateTime();
    m_deletedRemote.clear();
    m_addModRemote.clear();

    // do the sync process as described in twowaycontactsyncadapter.h
    if (!initSyncAdapter(m_accountId)) emit failed();
    if (!readSyncStateData(&m_remoteSince, m_accountId)) emit failed();
    determineRemoteChanges(m_remoteSince, m_accountId);
    // continued in gotRemoteChangesSince().
}

void TestSyncAdapter::determineRemoteChanges(const QDateTime &, const QString &)
{
    QTimer::singleShot(50, this, SLOT(continueTwoWaySync()));
}

void TestSyncAdapter::upsyncLocalChanges(const QDateTime &,
                                         const QList<QContact> &,
                                         const QList<QContact> &,
                                         const QList<QContact> &,
                                         const QString &)
{
    QTimer::singleShot(50, this, SLOT(finalizeTwoWaySync()));
}

void TestSyncAdapter::continueTwoWaySync()
{
    // simulated server-side changes:
    QPair<QList<QContact>, QList<QContact> > delAddModLists = m_remoteDelta.value(m_accountId);
    m_deletedRemote = delAddModLists.first; m_addModRemote = delAddModLists.second;

    // continuing the sync process as described in twowaycontactsyncadapter.h
    if (!storeRemoteChanges(m_deletedRemote, m_addModRemote, m_accountId)) emit failed();
    QList<QContact> locallyAdded, locallyModified, locallyDeleted;
    QDateTime localSince;
    if (!determineLocalChanges(&localSince, &locallyAdded, &locallyModified, &locallyDeleted, m_accountId)) emit failed();
    upsyncLocalChanges(localSince, locallyAdded, locallyModified, locallyDeleted, m_accountId);
    // continued in upsyncCompletedSuccessfully()
}

void TestSyncAdapter::finalizeTwoWaySync()
{
    if (!storeSyncStateData(m_accountId)) emit failed();
    emit finished(); // succeeded.
}

