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

#ifndef TWOWAYCONTACTSYNCADAPTER_H
#define TWOWAYCONTACTSYNCADAPTER_H

#include <QDateTime>
#include <QString>
#include <QList>
#include <QPair>
#include <QMap>

#include <QContactDetail>
#include <QContact>

QTCONTACTS_USE_NAMESPACE

namespace QtContactsSqliteExtensions {
class TwoWayContactSyncAdapterPrivate;
class TwoWayContactSyncAdapter
{
public:
    TwoWayContactSyncAdapter(const QString &syncTarget, const QMap<QString, QString> &params = QMap<QString, QString>());
    TwoWayContactSyncAdapter(const QString &syncTarget, QContactManager &mananger);
    virtual ~TwoWayContactSyncAdapter();

    // step one: init the sync adapter
    virtual bool initSyncAdapter(const QString &accountId, const QString &oobIdentifier = QString());
    // step two: read sync state data, including remoteSince value needed in step three
    virtual bool readSyncStateData(QDateTime *remoteSince, const QString &accountId);
    // step three: request server-side changes from remote service since remoteSince
    //   this is asynchronous and implementation-specific.
    virtual void determineRemoteChanges(const QDateTime &remoteSince, const QString &accountId);
    // step four: store those changes to the local database.
    virtual bool storeRemoteChanges(const QList<QContact> &deletedRemote,
                                    const QList<QContact> &addModRemote,
                                    const QString &accountId,
                                    bool needToApplyDelta = true);
    // step five: determine which contact changes occurred locally.
    virtual bool determineLocalChanges(QDateTime *localSince,
                                       QList<QContact> *locallyAdded,
                                       QList<QContact> *locallyModified,
                                       QList<QContact> *locallyDeleted,
                                       const QString &accountId);
    // step six: store those changes to the remote server
    //   this is asynchronous and implementation-specific.
    virtual void upsyncLocalChanges(const QDateTime &localSince,
                                    const QList<QContact> &locallyAdded,
                                    const QList<QContact> &locallyModified,
                                    const QList<QContact> &locallyDeleted,
                                    const QString &accountId);
    // step seven: store sync state data as required.
    virtual bool storeSyncStateData(const QString &accountId);
    // error case: purge sync state data so that the next sync is "clean".
    virtual bool purgeSyncStateData(const QString &accountId);

    bool removeAllContacts();

    const QContactManager &contactManager() const;
    QContactManager &contactManager();

protected:
    virtual void ensureAccountProvenance(QList<QContact> *locallyAdded,
                                         QList<QContact> *locallyModified,
                                         QList<QContact> *locallyDeleted,
                                         const QList<QContact> &prevRemote,
                                         const QList<QContactId> &exportedIds,
                                         const QString &accountId);
    virtual QList<QContactDetail> determineModifications(
                    QList<QContactDetail> *removalsOfThisType,
                    QList<QContactDetail> *additionsOfThisType) const;
    QList<QPair<QContact, QContact> > createUpdateList(
                    const QList<QContact> &prevRemote,
                    const QList<QContact> &remoteRemoved,
                    const QList<QContact> &remoteAddedModified,
                    QList<QContactId> *exportedIds,
                    QList<QContact> *mutatedPrevRemote,
                    bool needToApplyDelta = true) const;
    QContact applyRemoteDeltaToPrev(const QContact &prev, const QContact &curr) const;

private:
    QDateTime maxModificationTimestamp(const QList<QContact> &contacts) const;
    QPair<QList<QContactDetail>, QList<QContactDetail> > fallbackDelta(const QContact &prev, const QContact &curr) const;
    QList<QContactDetail> improveDelta(QList<QContactDetail> *removals, QList<QContactDetail> *additions) const;
    int scoreForDetailPair(const QContactDetail &removal, const QContactDetail &addition) const;
    int scoreForValuePair(const QVariant &removal, const QVariant &addition) const;
    bool detailPairExactlyMatches(const QContactDetail &a, const QContactDetail &b) const;
    int exactDetailMatchExistsInList(const QContactDetail &det, const QList<QContactDetail> &list) const;
    void removeIgnorableDetailsFromList(QList<QContactDetail> *dets) const;
    int exactContactMatchExistsInList(const QContact &c, const QList<QContact> &list) const;

protected:
    void dumpContact(const QContact &c) const; // debugging.
    TwoWayContactSyncAdapterPrivate *d;
};
}

#endif // TWOWAYCONTACTSYNCADAPTER_H
