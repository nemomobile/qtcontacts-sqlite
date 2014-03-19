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

#ifndef TWOWAYCONTACTSYNCADAPTER_IMPL_H
#define TWOWAYCONTACTSYNCADAPTER_IMPL_H

#include <qtcontacts-extensions.h>
#include <contactmanagerengine.h>
#include <twowaycontactsyncadapter.h>
#include <qcontactoriginmetadata.h>
#include <private/qcontactmanager_p.h>

#include <QContactManager>
#include <QContactGuid>
#include <QContactSyncTarget>
#include <QContactTimestamp>
#include <QContactUrl>

namespace QtContactsSqliteExtensions {
    class TwoWayContactSyncAdapterPrivate
    {
    public:
        enum Status {
            Inactive = 0,
            Initialized,
            ReadSyncStateData,
            StoredRemoteChanges,
            DeterminedLocalChanges,
            Finished
        };
        struct StateData {
            StateData() : m_status(Inactive), m_mutated(false) {}
            Status m_status;
            QString m_oobScope;
            QDateTime m_localSince;
            QDateTime m_remoteSince;
            QDateTime m_newRemoteSince;
            QList<QContactId> m_exportedIds;
            QList<QContact> m_prevRemote;
            QList<QContact> m_mutatedPrevRemote;
            bool m_mutated; // whether the MUTATED_PREV_REMOTE list has been populated or not
        };

        TwoWayContactSyncAdapterPrivate(const QString &syncTarget, const QMap<QString, QString> &params);
        TwoWayContactSyncAdapterPrivate(const QString &syncTarget, QContactManager &manager);
       ~TwoWayContactSyncAdapterPrivate();
        QContactManager *m_manager;
        ContactManagerEngine *m_engine;
        QString m_syncTarget;
        bool m_deleteManager;

        void clear(const QString &accountId);        // clears the state data below.
        QMap<QString, StateData> m_stateData; // per account.
    };
}

QTCONTACTS_USE_NAMESPACE
using namespace QtContactsSqliteExtensions;

static void registerTypes()
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        qRegisterMetaType<QList<int> >();
        qRegisterMetaTypeStreamOperators<QList<int> >();
    }
}

TwoWayContactSyncAdapterPrivate::TwoWayContactSyncAdapterPrivate(const QString &syncTarget, const QMap<QString, QString> &params)
    : m_manager(new QContactManager(QStringLiteral("org.nemomobile.contacts.sqlite"), params))
    , m_engine(contactManagerEngine(*m_manager))
    , m_syncTarget(syncTarget)
    , m_deleteManager(true)
{
    registerTypes();
}

TwoWayContactSyncAdapterPrivate::TwoWayContactSyncAdapterPrivate(const QString &syncTarget, QContactManager &manager)
    : m_manager(&manager)
    , m_engine(contactManagerEngine(*m_manager))
    , m_syncTarget(syncTarget)
    , m_deleteManager(false)
{
    registerTypes();
}

TwoWayContactSyncAdapterPrivate::~TwoWayContactSyncAdapterPrivate()
{
    if (m_deleteManager) {
        delete m_manager;
    }
}

void TwoWayContactSyncAdapterPrivate::clear(const QString &accountId)
{
    m_stateData.insert(accountId, TwoWayContactSyncAdapterPrivate::StateData());
}

// ----------------------------------------

TwoWayContactSyncAdapter::TwoWayContactSyncAdapter(const QString &syncTarget, const QMap<QString, QString> &params)
    : d(new TwoWayContactSyncAdapterPrivate(syncTarget, params))
{
}

TwoWayContactSyncAdapter::TwoWayContactSyncAdapter(const QString &syncTarget, QContactManager &manager)
    : d(new TwoWayContactSyncAdapterPrivate(syncTarget, manager))
{
}

TwoWayContactSyncAdapter::~TwoWayContactSyncAdapter()
{
    delete d;
}

// step one: init the sync adapter
bool TwoWayContactSyncAdapter::initSyncAdapter(const QString &accountId, const QString &oobIdentifier)
{
    if (d->m_stateData[accountId].m_status != TwoWayContactSyncAdapterPrivate::Inactive) {
        qWarning() << Q_FUNC_INFO << "already busy with another sync";
        return false;
    }

    if (!d->m_engine) {
        qWarning() << Q_FUNC_INFO << "no connection to qtcontacts-sqlite";
        return false;
    }

    d->m_stateData[accountId].m_oobScope = oobIdentifier.isEmpty() ? QStringLiteral("%1-%2").arg(d->m_syncTarget).arg(accountId) : oobIdentifier;
    d->m_stateData[accountId].m_status = TwoWayContactSyncAdapterPrivate::Initialized;
    return true;
}

// step two: read state data from the qtcontacts-sqlite oob database
bool TwoWayContactSyncAdapter::readSyncStateData(QDateTime *remoteSince, const QString &accountId)
{
    if (d->m_stateData[accountId].m_status != TwoWayContactSyncAdapterPrivate::Initialized) {
        qWarning() << Q_FUNC_INFO << "invalid state" << d->m_stateData[accountId].m_status;
        return false;
    }

    // Read the timestamps we should specify when requesting changes
    // from both the remote service and the local database, and also
    // the PREV_REMOTE and EXPORTED_IDS lists, from the out-of-band database.
    // The PREV_REMOTE list is the list of contacts which we upsynced to the remote
    // server during the last sync run.  The EXPORTED_IDS list is the list of ids
    // of contacts which we upsynced last run which do not have a synctarget constituent
    // associated with them (and so we need to explicitly tell the qtcontacts-sqlite
    // backend that we're interested in receiving change information about it).
    QMap<QString, QVariant> values;
    QStringList keys;
    keys << QStringLiteral("remoteSince") << QStringLiteral("localSince");
    keys << QStringLiteral("prevRemote") << QStringLiteral("exportedIds");
    if (!d->m_engine->fetchOOB(d->m_stateData[accountId].m_oobScope, keys, &values)) {
        // fetchOOB only returns false if a db error occurs; it still returns true
        // if the fetch "succeeded" but no values for those keys exist.
        qWarning() << Q_FUNC_INFO << "failed to read sync state data for" << d->m_syncTarget << "account" << accountId;
        d->clear(accountId);
        return false;
    }

    QString sinceStr = values.value(QStringLiteral("remoteSince")).toString();
    d->m_stateData[accountId].m_remoteSince = sinceStr.isEmpty()
                                            ? QDateTime()
                                            : QDateTime::fromString(sinceStr, Qt::ISODate);

    sinceStr = values.value(QStringLiteral("localSince")).toString();
    d->m_stateData[accountId].m_localSince = sinceStr.isEmpty()
                                           ? QDateTime()
                                           : QDateTime::fromString(sinceStr, Qt::ISODate);

    QByteArray cdata = values.value(QStringLiteral("prevRemote")).toByteArray();
    QDataStream readPrevRemote(cdata);
    readPrevRemote >> d->m_stateData[accountId].m_prevRemote;

    cdata = values.value(QStringLiteral("exportedIds")).toByteArray();
    QDataStream readExportedIds(cdata);
    readExportedIds >> d->m_stateData[accountId].m_exportedIds;

    // the next step is to sync down from the remote database,
    // so we can assume that we will get all changes which
    // occurred up until the current datetime.
    // thus, the next time we sync, we should use the current
    // datetime as the "since" value when retrieving changes.
    d->m_stateData[accountId].m_newRemoteSince = QDateTime::currentDateTimeUtc();

    // return the current remoteSince value for this sync.
    *remoteSince = d->m_stateData[accountId].m_remoteSince;
    d->m_stateData[accountId].m_status = TwoWayContactSyncAdapterPrivate::ReadSyncStateData;
    return true;
}


// step three: determine changes lists from the remote service.
void TwoWayContactSyncAdapter::determineRemoteChanges(const QDateTime &remoteSince, const QString &accountId)
{
    Q_UNUSED(remoteSince)
    Q_UNUSED(accountId)

    // asynchronous and implementation specific.  Once done, call storeRemoteChanges().
    // Important note: sync adapter implementations MUST ensure the following:
    //  - every contact it receives from the remote service MUST have a QContactGuid detail in it
    //    and that guid value should be of the form <accountId>:<remoteGuid>
    //  - every contact it receives from the remote service which has a custom X-QCOMDGID property
    //    value MUST have that value set in a QContactOriginMetadata::groupid() detail field.
    // See step six for more information.
}

// step four: store the server-side changes to the local database.
bool TwoWayContactSyncAdapter::storeRemoteChanges(const QList<QContact> &deletedRemote,
                                                  const QList<QContact> &addModRemote,
                                                  const QString &accountId)
{
    if (d->m_stateData[accountId].m_status != TwoWayContactSyncAdapterPrivate::ReadSyncStateData) {
        qWarning() << Q_FUNC_INFO << "invalid state" << d->m_stateData[accountId].m_status;
        return false;
    }

    // mutate contacts from PREV_REMOTE according to the changes which occurred remotely.
    // depending on the change, the contact might need to be removed from the exportedIds list.
    // e.g., if the contact is deleted remotely, then we're no longer interested in syncing
    // that local contact up to the remote server, even if it changes on the local device in
    // the future.
    // the first value in each pair will be a copy of a value in the prevRemote list.
    // the second value in each pair will be a copy of a (newly constructed) value in the mutatedPrevRemote list.
    QList<QPair<QContact, QContact> > syncContactUpdates = createUpdateList(d->m_stateData[accountId].m_prevRemote,
                                                                            deletedRemote, addModRemote,
                                                                            &d->m_stateData[accountId].m_exportedIds,
                                                                            &d->m_stateData[accountId].m_mutatedPrevRemote);
    d->m_stateData[accountId].m_mutated = true; // createUpdateList will populate MUTATED_PREV_REMOTE from PREV_REMOTE

    // store them to qtcontacts-sqlite.
    // Note that because of some magic in the mutation code in createUpdateList(), the
    // backend will be able to perfectly determine the change delta on a per-detail level.
    // Importantly, it is possible that the user locally deleted one of the contacts for
    // which we're trying to save modifications to - in that case, the qtcontacts-sqlite
    // backend will not write those modifications, as the contact no longer exists,
    // and the deletion of that contact will be reported in the locallyDeletedIds list
    // in the next qtcontacts-sqlite fetchSyncContacts() call (in the sixth step).
    QContactManager::Error error;
    if (syncContactUpdates.size()) {
        if (!d->m_engine->storeSyncContacts(d->m_syncTarget,
                                            QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges,
                                            syncContactUpdates,
                                            &error)) {
            qWarning() << Q_FUNC_INFO << "error - couldn't store sync contacts!";
            d->clear(accountId);
            return false;
        }
    } else {
        qDebug() << Q_FUNC_INFO << "no substantial changes, no need to store remote changes.";
    }

    d->m_stateData[accountId].m_status = TwoWayContactSyncAdapterPrivate::StoredRemoteChanges;
    return true;
}

// step five: ask qtcontacts-sqlite for changes which occurred locally since last sync.  Note: localSince is an outparam!
bool TwoWayContactSyncAdapter::determineLocalChanges(QDateTime *localSince,
                                                     QList<QContact> *locallyAdded,
                                                     QList<QContact> *locallyModified,
                                                     QList<QContact> *locallyDeleted,
                                                     const QString &accountId)
{
    if (d->m_stateData[accountId].m_status != TwoWayContactSyncAdapterPrivate::StoredRemoteChanges) {
        qWarning() << Q_FUNC_INFO << "invalid state" << d->m_stateData[accountId].m_status;
        return false;
    }

    // read synctarget+exported contacts which have been added/modified/removed since
    // the last local since timestamp.
    // Importantly, this WILL include the changes we made in the fifth step above.
    // This means that we need to look over the list and remove any contact which exactly
    // matches an entry in the MUTATED_PREV_REMOTE list (as there is no new changes in those
    // contacts which doesn't already exist server-side).
    // Note: if localSince isn't valid, then we're doing a clean sync, and we can
    // to pass in zero-pointers for modified/deleted list (or at least, ignore them)
    // (otherwise enabling/disabling/re-enabling an account could cause local deletions
    //  to be pushed upstream, which would be very bad (tm)).
    QContactManager::Error error;
    QList<QContactId> locallyDeletedIds;
    bool cleanSync = d->m_stateData[accountId].m_localSince.isValid() ? false : true;
    if (!d->m_engine->fetchSyncContacts(d->m_syncTarget,
                                        d->m_stateData[accountId].m_localSince,
                                        d->m_stateData[accountId].m_exportedIds,
                                        locallyModified,
                                        locallyAdded,
                                        &locallyDeletedIds,
                                        &error)) {
        qWarning() << Q_FUNC_INFO << "error - couldn't fetch locally modified sync contacts!";
        d->clear(accountId);
        return false;
    }

    // Depending on the order with which the sync functions are called,
    // we may not have yet populated the MUTATED_PREV_REMOTE data from PREV_REMOTE.
    if (!d->m_stateData[accountId].m_mutated) {
        d->m_stateData[accountId].m_mutated = true;
        d->m_stateData[accountId].m_mutatedPrevRemote = d->m_stateData[accountId].m_prevRemote;
    }

    // we treat deletions/modifications differently in clean sync case.
    if (cleanSync) {
        // a) ignore these deletions.  We're clean syncing and CANNOT clobber the remote data.
        locallyDeletedIds.clear();

        // b) update our m_mutatedPrevRemote with the values in the
        //    locallyModified list and then clear locallyModified.
        if (locallyModified) {
            for (int i = 0; i < locallyModified->size(); ++i) {
                const QContact &lmc(locallyModified->at(i));
                const QString &lmguid = lmc.detail<QContactGuid>().guid();
                for (int j = 0; j < d->m_stateData[accountId].m_mutatedPrevRemote.size(); ++j) {
                    if (d->m_stateData[accountId].m_mutatedPrevRemote[j].detail<QContactGuid>().guid() == lmguid) {
                        // found matching prevRemote - replace it with the updated version.
                        // this will ensure that the MUTATED_PREV_REMOTE version has the correct contactId.
                        d->m_stateData[accountId].m_mutatedPrevRemote.replace(j, lmc);
                        break;
                    }
                }
            }
            locallyModified->clear();
        }
    } else {
        // a) find contacts which were deleted locally
        if (locallyDeleted) {
            bool foundPrevToDelete = false;
            foreach (const QContactId &id, locallyDeletedIds) {
                foundPrevToDelete = false;
                for (int i = 0; i < d->m_stateData[accountId].m_mutatedPrevRemote.size(); ++i) {
                    const QContact &prev(d->m_stateData[accountId].m_mutatedPrevRemote[i]);
                    if (prev.id() == id) {
                        locallyDeleted->append(prev);
                        d->m_stateData[accountId].m_mutatedPrevRemote.removeAt(i); // we are deleting this contact from the remote server.
                        foundPrevToDelete = true;
                        break;
                    }
                }

                if (!foundPrevToDelete) {
                    // it was already deleted remotely and removed from the MUTATED_PREV_REMOTE list.
                    qWarning() << Q_FUNC_INFO << "contact with id:" << id.toString() << "removed locally + remotely";
                }
            }
        }

        // b) remove from the lists of changes to upsync any contact which isn't actually
        //    different to the corresponding contact in the MUTATED_PREV_REMOTE list.
        //    In that case, however, we do need to update the corresponding contact's id.
        for (int i = 0; i < d->m_stateData[accountId].m_mutatedPrevRemote.size(); ++i) {
            // check this contact to see whether it already represents one from the changes lists.
            QContact remoteContact = d->m_stateData[accountId].m_mutatedPrevRemote.at(i);
            if (locallyAdded) {
                int matchIndex = exactContactMatchExistsInList(remoteContact, *locallyAdded);
                if (matchIndex != -1) {
                    remoteContact = locallyAdded->takeAt(matchIndex);
                    d->m_stateData[accountId].m_mutatedPrevRemote.replace(i, remoteContact);
                }
            }
            if (locallyModified) {
                int matchIndex = exactContactMatchExistsInList(remoteContact, *locallyModified);
                if (matchIndex != -1) {
                    remoteContact = locallyModified->takeAt(matchIndex);
                    d->m_stateData[accountId].m_mutatedPrevRemote.replace(i, remoteContact);
                }
            }
        }
    }

    // c) any locally added contact which we intend to upsync should be added
    //    to our list of exportedIds.
    //    Note that locallyModified/locallyDeleted will only contain
    //    local-constituent-only contacts if they were identified in the
    //    exportedIds list which we passed into the fetchSyncContacts() function.
    if (locallyAdded) {
        for (int i = 0; i < locallyAdded->size(); ++i) {
            QContact localContact = locallyAdded->at(i);
            d->m_stateData[accountId].m_exportedIds.append(localContact.id());

            // we remove from the local contact any guid detail
            // as we need to use the guid detail to store the remote UID of
            // the contact.  In any event, a guid must be globally unique in the
            // database, so we couldn't store it after round-trip in the synctarget
            // constituent or we'd violate the global-uniqueness constraint.
            QContactGuid localContactGuid = localContact.detail<QContactGuid>();
            if (!localContactGuid.isEmpty()) {
                qWarning() << Q_FUNC_INFO << "Clobbering local QContactGuid - cannot sync this detail.";
                localContact.removeDetail(&localContactGuid);
            }

            // we also need to add the id as a X-QCOMDGID field, so that we can do
            // a reverse-mapping the next time we down-sync server-side changes.
            QContactOriginMetadata omd = localContact.detail<QContactOriginMetadata>();
            if (!omd.groupId().isEmpty()) {
                qWarning() << Q_FUNC_INFO << "Clobbering QContactOriginMetadata::groupId() - cannot sync this field.";
            }
            omd.setGroupId(localContact.id().toString());
            localContact.saveDetail(&omd);
            locallyAdded->replace(i, localContact);
        }
    }

    // d) ensure that the added/modified lists contain the appropriate content for the current account.
    ensureAccountProvenance(locallyAdded, locallyModified, locallyDeleted,
                            d->m_stateData[accountId].m_mutatedPrevRemote,
                            d->m_stateData[accountId].m_exportedIds,
                            accountId);

    // e) now further mutate MUTATED_PREV_REMOTE to include the local changes which we
    //    are just about to upsync to the remote server.
    if (locallyAdded) {
        for (int i = 0; i < locallyAdded->size(); ++i) {
            d->m_stateData[accountId].m_mutatedPrevRemote.append(locallyAdded->at(i));
        }
    }
    if (locallyModified) {
        bool foundToReplace = false;
        for (int i = 0; i < locallyModified->size(); ++i) {
            const QString &lguid(locallyModified->at(i).detail<QContactGuid>().guid());
            const QString &lgid(locallyModified->at(i).id().toString()); // TODO: is this stable?  Could it change due to was_local?
            foundToReplace = false;
            for (int j = 0; j < d->m_stateData[accountId].m_mutatedPrevRemote.size(); ++j) {
                const QString &pguid(d->m_stateData[accountId].m_mutatedPrevRemote[j].detail<QContactGuid>().guid());
                const QString &pgid(d->m_stateData[accountId].m_mutatedPrevRemote[j].detail<QContactOriginMetadata>().groupId());
                if (lguid == pguid || lgid == pgid) {
                    foundToReplace = true;
                    d->m_stateData[accountId].m_mutatedPrevRemote.replace(j, locallyModified->at(i));
                    break;
                }
            }

            if (!foundToReplace) {
                // we shouldn't treat this as a local addition, this is always a bug.
                qWarning() << Q_FUNC_INFO << "FIXME: local modification reported for non-upsynced local contact";
            }
        }
    }

    // done.
    d->m_stateData[accountId].m_status = TwoWayContactSyncAdapterPrivate::DeterminedLocalChanges;
    *localSince = d->m_stateData[accountId].m_localSince;
    return true;
}

// step six: sync the changes up to the remote server.
void TwoWayContactSyncAdapter::upsyncLocalChanges(const QDateTime &localSince,
                                                  const QList<QContact> &locallyAdded,
                                                  const QList<QContact> &locallyModified,
                                                  const QList<QContact> &locallyDeleted,
                                                  const QString &accountId)
{
    Q_UNUSED(localSince)
    Q_UNUSED(locallyAdded)
    Q_UNUSED(locallyModified)
    Q_UNUSED(locallyDeleted)
    Q_UNUSED(accountId)

    // asynchronous and implementation specific.  when finished, call storeSyncStateData().
    // Important note: sync adapter implementations MUST ensure the following:
    //  - any contact which is being upsynced which does NOT have a pre-existing
    //    remote counterpart (and therefore, does not have a QContactGuid detail;
    //    ie, a locallyAdded contact) MUST be upsynced with a custom X-QCOMDGID
    //    value set (from the QContactOriginMetadata::groupId() field value).
    //  - any contact which is being upsynced which DOES have a pre-existing
    //    remote counterpart (and therefore, has a QContactGuid detail), may
    //    have that X-QCOMDGID field deleted/removed as it is no longer used
    //    for mapping the local-to-remote once a Guid is available.
    // See step three for more information.
}

// step seven: store state data to qtcontacts-sqlite oob database.
bool TwoWayContactSyncAdapter::storeSyncStateData(const QString &accountId)
{
    if (d->m_stateData[accountId].m_status != TwoWayContactSyncAdapterPrivate::DeterminedLocalChanges) {
        qWarning() << Q_FUNC_INFO << "invalid state" << d->m_stateData[accountId].m_status;
        return false;
    }

    QMap<QString, QVariant> values;

    // store the MUTATED_PREV_REMOTE list into oob as PREV_REMOTE for next time.
    QByteArray cdata;
    QDataStream write(&cdata, QIODevice::WriteOnly);
    write << d->m_stateData[accountId].m_mutatedPrevRemote;
    values.insert(QStringLiteral("prevRemote"), QVariant(cdata));

    // also store the EXPORTED_IDS list into oob to track non-synctarget contacts we upsynced.
    cdata.clear();
    QDataStream writeExportedIds(&cdata, QIODevice::WriteOnly);
    writeExportedIds << d->m_stateData[accountId].m_exportedIds;
    values.insert(QStringLiteral("exportedIds"), QVariant(cdata));

    // finally, determine new local since timestamp and store both of them to oob.
    // NOTE: the newLocalSince timestamp may miss updates due to qtcontacts-sqlite setting modified timestamp. TODO!
    QDateTime newLocalSince = maxModificationTimestamp(d->m_stateData[accountId].m_mutatedPrevRemote);
    if (!newLocalSince.isValid()) {
        // if no changes have ever occurred locally, we can use the same timestamp we
        // will use for the remote since, as that timestamp is from a valid time prior
        // to when we retrieved local changes during this sync run.
        newLocalSince = d->m_stateData[accountId].m_newRemoteSince;
    }
    values.insert(QStringLiteral("remoteSince"), QVariant(d->m_stateData[accountId].m_newRemoteSince));
    values.insert(QStringLiteral("localSince"), QVariant(newLocalSince));

    // perform the store operation to the oob db.
    if (!d->m_engine->storeOOB(d->m_stateData[accountId].m_oobScope, values)) {
        qWarning() << Q_FUNC_INFO << "error - couldn't store sync state data to oob!";
        d->clear(accountId);
        return false;
    }

    // finished the sync process successfully.
    d->m_stateData[accountId].m_status = TwoWayContactSyncAdapterPrivate::Finished;
    qWarning() << Q_FUNC_INFO << "Sync process succeeded at"
               << QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    d->clear(accountId); // this actually sets state back to Inactive as required.
    return true;
}

// error case: purge state data from qtcontacts-sqlite oob database.
bool TwoWayContactSyncAdapter::purgeSyncStateData(const QString &accountId)
{
    bool purgeSucceeded = true;
    QStringList purgeKeys;
    purgeKeys << QStringLiteral("prevRemote") << QStringLiteral("exportedIds");
    purgeKeys << QStringLiteral("remoteSince") << QStringLiteral("localSince");

    if (!d->m_engine->removeOOB(d->m_stateData[accountId].m_oobScope, purgeKeys)) {
        qWarning() << Q_FUNC_INFO << "error - couldn't purge state data from oob!";
        purgeSucceeded = false;
    }

    d->clear(accountId);
    return purgeSucceeded;
}

bool TwoWayContactSyncAdapter::removeAllContacts()
{
    QContactDetailFilter syncTargetFilter;
    syncTargetFilter.setDetailType(QContactSyncTarget::Type, QContactSyncTarget::FieldSyncTarget);
    syncTargetFilter.setValue(d->m_syncTarget);

    QContactDetailFilter typeFilter;
    typeFilter.setDetailType(QContactType::Type, QContactType::FieldType);
    typeFilter.setValue(QContactType::TypeContact);

    // Remove all contacts stored for our syncTarget
    QList<QContactId> syncTargetIds(d->m_manager->contactIds(syncTargetFilter & typeFilter));
    if (!syncTargetIds.isEmpty()) {
        if (!d->m_manager->removeContacts(syncTargetIds)) {
            qWarning() << Q_FUNC_INFO << "error - couldn't remove" << syncTargetIds.count() << "existing contacts!";
            return false;
        }
    }

    return true;
}

const QContactManager &TwoWayContactSyncAdapter::contactManager() const
{
    return *d->m_manager;
}

QContactManager &TwoWayContactSyncAdapter::contactManager()
{
    return *d->m_manager;
}

// ------------------------------------------------------------

// search through the list of contacts, determine the most recent timestamp datetime.
QDateTime TwoWayContactSyncAdapter::maxModificationTimestamp(const QList<QContact> &contacts) const
{
    QDateTime since;

    for (int i = 0; i < contacts.size(); ++i) {
        const QContactTimestamp &ts(contacts[i].detail<QContactTimestamp>());
        if (ts.lastModified().isValid() && (ts.lastModified() > since || !since.isValid())) {
            since = ts.lastModified();
        } else if (ts.created().isValid() && (ts.created() > since || !since.isValid())) {
            since = ts.created();
        }
    }

    return since;
}

// The function is called to remove from the lists of local changes which
// are reported by the qtcontacts-sqlite backend any contacts which are
// not from the correct account.
// Implementations can override this function if they store such information
// in a different way; the default implementation uses QContactGuid,
// according to the formatting specified in the comment within getRemoteChangesSince().
void TwoWayContactSyncAdapter::ensureAccountProvenance(QList<QContact> *locallyAdded,
                                                       QList<QContact> *locallyModified,
                                                       QList<QContact> *locallyDeleted,
                                                       const QList<QContact> &prevRemote,
                                                       const QList<QContactId> &exportedIds,
                                                       const QString &accountId)
{
    // The default implementation doesn't need to look at the prevRemote version.
    // Other implementations may need to check if a field value changes (eg OriginMetadata groupId)
    Q_UNUSED(prevRemote)

    // local additions are always upsynced, so we don't need to remove contacts from this list
    // but we may need to modify which details each contact contains, as we don't upsync some
    // details from local contacts (eg, guids from other constituents make no sense).
    if (locallyAdded) {
        for (int i = 0; i < locallyAdded->size(); ++i) {
            QContact c = locallyAdded->at(i);
            QContactGuid cguid = c.detail<QContactGuid>();
            cguid.setGuid(QString());
            c.saveDetail(&cguid);
            locallyAdded->replace(i, c);
        }
    }

    // For deletions and modifications, we check to see if guid starts with <accountId>.
    // If not, we remove it from the list (as it was never in this account).
    // Due to the way the remoteSince timestamp is calculated (ie, it's the timestamp of when
    // readSyncStateData() was called during the previous sync, basically), a previously-upsynced
    // contact will be returned as a remote addition in the server-side-changeset during a
    // previous step, and we can map it to the correct entry in the prevRemote list via the
    // contactId (constructed from the custom property which must store the contactId in stringified
    // form, which must have been upsynced according to the contract).  Thus, it will get a guid.
    // In short, any locallyModified/locallyDeleted contact which was previously upsynced,
    // WILL in this function already have a QContactGuid detail within it, from the correct constituent.
    QString accountIdPrefix = accountId + QChar::fromLatin1(':');
#define FILTER_NONMATCHING_CONTACTS(list) \
    if (list) {                                                                                               \
        do {                                                                                                  \
            for (int i = list->size() - 1; i >= 0; --i) {                                                     \
                if (exportedIds.contains(list->at(i).id())) {                                                 \
                    /* This contact might not have been downsynced since it was upsynced, but is exported. */ \
                    continue;                                                                                 \
                }                                                                                             \
                if (!list->at(i).detail<QContactGuid>().guid().startsWith(accountIdPrefix)) {                 \
                    /* This contact was modified/deleted locally, but was never upsynced to this account. */  \
                    list->removeAt(i);                                                                        \
                }                                                                                             \
            }                                                                                                 \
        } while (0);                                                                                          \
    }
    FILTER_NONMATCHING_CONTACTS(locallyDeleted);
    FILTER_NONMATCHING_CONTACTS(locallyModified);
#undef FILTER_NONMATCHING_CONTACTS
}

// Given the list of PREV_REMOTE contacts which were stored in qtcontacts-sqlite's out-of-band database
// at the successful completion of the last sync run by this sync adapter, and also the
// lists of remotely added/modified/deleted contacts, plus the list of "exportedIds" (which
// contains the QContactIds of previously-upsynced contacts which did not have a synctarget
// constituent (and therefore no GUID detail)), this function will create a list of update
// pairs which can be passed to the qtcontacts-sqlite storeSyncContacts() function.
QList<QPair<QContact, QContact> > TwoWayContactSyncAdapter::createUpdateList(const QList<QContact> &prevRemote,
                                                                             const QList<QContact> &remoteDeleted,
                                                                             const QList<QContact> &remoteAddedModified,
                                                                             QList<QContactId> *exportedIds,
                                                                             QList<QContact> *mutatedPrevRemote) const
{
    // <PREV_REMOTE, UPDATED_REMOTE> pairs.
    QList<QPair<QContact, QContact> > retn;

    // build up some hashes for easy lookup.
    QHash<QString, int> prevGuidToIndex;
    QHash<QString, int> prevGIdToIndex;
    for (int i = 0; i < prevRemote.size(); ++i) {
        if (prevRemote[i].detail<QContactGuid>().guid().isEmpty()) {
            // This contact did not have a sync-target constituent
            // when we synced it up last time (ie, it was a "local" contact)
            // Instead of storing the QContactGuid in the hash, we store the
            // QContactOriginMetadata::groupId()
            QString gid = prevRemote[i].detail<QContactOriginMetadata>().groupId();
            if (gid.isEmpty()) {
                qWarning() << Q_FUNC_INFO << "error: contact has no origin metadata";
            } else {
                prevGIdToIndex.insert(gid, i);
            }
        } else {
            prevGuidToIndex.insert(prevRemote[i].detail<QContactGuid>().guid(), i);
        }
    }
    QHash<QString, int> deletedGuidToIndex;
    QHash<QString, QString> deletedGuidToGId;
    for (int i = 0; i < remoteDeleted.size(); ++i) {
        // all contacts from the remote service will have a Guid.
        QString remoteGuid = remoteDeleted[i].detail<QContactGuid>().guid();
        deletedGuidToIndex.insert(remoteGuid, i);
        QString gid = remoteDeleted[i].detail<QContactOriginMetadata>().groupId();
        if (!gid.isEmpty()) {
            // if the gid was stored server-side (as a custom/extended property)
            // then it may have been because when we synced it up previously,
            // it did not have a sync-target constituent associated with it.
            deletedGuidToGId.insert(remoteGuid, gid);
        }
    }
    QHash<QString, int> addedModifiedGuidToIndex;
    QHash<QString, QString> addedModifiedGuidToGId;
    for (int i = 0; i < remoteAddedModified.size(); ++i) {
        // all contacts from the remote service will have a Guid.
        QString remoteGuid = remoteAddedModified[i].detail<QContactGuid>().guid();
        addedModifiedGuidToIndex.insert(remoteGuid, i);
        QString gid = remoteAddedModified[i].detail<QContactOriginMetadata>().groupId();
        if (!gid.isEmpty()) {
            // if the gid was stored server-side (as a custom/extended property)
            // then it may have been because when we synced it up previously,
            // it did not have a sync-target constituent associated with it.
            addedModifiedGuidToGId.insert(remoteGuid, gid);
        }
    }

    // determine remote removals: <PREV_REMOTE, NULL>
    QList<int> prevRemoteDeletionIndexes;
    foreach (const QString &guid, deletedGuidToIndex.keys()) {
        if (prevGuidToIndex.contains(guid)) {
            // When we synced it up, it had a synctarget constituent.
            // The qtcontacts-sqlite backend will delete that constituent, but not
            // any local-constituent data which is associated with the partial
            // aggregate (as that data is not owned by this sync adapter).
            int prdIndex = prevGuidToIndex.value(guid);
            prevRemoteDeletionIndexes.append(prdIndex);
            retn.append(qMakePair(prevRemote.at(prdIndex), QContact()));
        } else if (deletedGuidToGId.contains(guid) && prevGIdToIndex.contains(deletedGuidToGId.value(guid))) {
            // When we synced it up, it did not have a synctarget constituent.
            // If something else has added a synctarget constituent in the meantime,
            // the backend will delete that, otherwise it will remain untouched.
            int prdIndex = prevGIdToIndex.value(deletedGuidToGId.value(guid));
            prevRemoteDeletionIndexes.append(prdIndex);
            retn.append(qMakePair(prevRemote.at(prdIndex), QContact()));
            // This sync adapter should remove that contact from the list of
            // exported ids (as it no longer wishes to sync it up to the remote).
            exportedIds->removeAll(QContactId::fromString(deletedGuidToGId.value(guid)));
        }
    }

    // determine remote additions/modifications
    QHash<int, QContact> prevRemoteModificationIndexes;
    QList<QContact> prevRemoteAdditions;
    foreach (const QString &guid, addedModifiedGuidToIndex.keys()) {
        if (prevGuidToIndex.contains(guid)) {
            // modifications: <PREV_REMOTE, UPDATED_REMOTE>
            // Note: we have to apply the delta to the old-prev contact
            // to ensure that the detail ids are preserved.
            int prmIndex = prevGuidToIndex.value(guid);
            const QContact &prev(prevRemote[prmIndex]);
            const QContact &curr(remoteAddedModified[addedModifiedGuidToIndex.value(guid)]);
            QContact updated = applyRemoteDeltaToPrev(prev, curr);
            if (exactContactMatchExistsInList(prev, QList<QContact>() << updated) == -1) {
                // the change is substantial (ie, wasn't just an eTag update, for example)
                prevRemoteModificationIndexes.insert(prmIndex, updated);
                retn.append(qMakePair(prev, updated));
            }
        } else if (addedModifiedGuidToGId.contains(guid) && prevGIdToIndex.contains(addedModifiedGuidToGId.value(guid))) {
            // When we synced it up last time, it did not have a synctarget constituent.
            // It was modified server side, and so when we store it this time, the
            // qtcontacts-sqlite backend will create a synctarget constituent within
            // which to store the added GUID detail, plus any other added details.
            // Note that any modifications to existing (local) details will be stored
            // as modifications in the original local constituent contact.
            // modifications: <PREV_REMOTE, UPDATED_REMOTE>
            int prmIndex = prevGIdToIndex.value(addedModifiedGuidToGId.value(guid));
            const QContact &prev(prevRemote[prmIndex]);
            const QContact &curr(remoteAddedModified[addedModifiedGuidToIndex.value(guid)]);
            QContact updated = applyRemoteDeltaToPrev(prev, curr);
            // No need to check if the change is substantial - it is, it now has a guid.
            prevRemoteModificationIndexes.insert(prmIndex, updated);
            retn.append(qMakePair(prev, updated));
        } else {
            // this is a pure server-side addition.
            // additions: <NULL, UPDATED_REMOTE>
            QContact addedContact = remoteAddedModified.at(addedModifiedGuidToIndex.value(guid));
            prevRemoteAdditions.append(addedContact);
            retn.append(qMakePair(QContact(), addedContact));
        }
    }

    // finally, mutate the mutatedPrevRemote list according to the changes.
    // modifications
    *mutatedPrevRemote = prevRemote;
    foreach (int idx, prevRemoteModificationIndexes.keys())
        mutatedPrevRemote->replace(idx, prevRemoteModificationIndexes.value(idx));
    qSort(prevRemoteDeletionIndexes);
    for (int i = prevRemoteDeletionIndexes.size() - 1; i >= 0; --i)
        mutatedPrevRemote->removeAt(prevRemoteDeletionIndexes.at(i));
    mutatedPrevRemote->append(prevRemoteAdditions);

    return retn;
}


// Having received the current server-side version of the contact,
// we need to determine what actually changed server-side.
// Once we determine what changed, we mutate PREV with those
// changes, so that the detail ids are preserved.
// This will allow qtcontacts-sqlite to precisely determine
// what changed remotely, and apply the changes appropriately
// in the local database.
QContact TwoWayContactSyncAdapter::applyRemoteDeltaToPrev(const QContact &prev, const QContact &curr) const
{
    QContact newRemote = prev;

    QPair<QList<QContactDetail>, QList<QContactDetail> > fbd = fallbackDelta(newRemote, curr);
    QList<QContactDetail> removals = fbd.first;
    QList<QContactDetail> additions = fbd.second;
    QList<QContactDetail> modifications = improveDelta(&removals, &additions);

    for (int i = 0; i < removals.size(); ++i) {
        QContactDetail removal = removals.at(i);
        newRemote.removeDetail(&removal);
    }

    for (int i = 0; i < additions.size(); ++i) {
        QContactDetail addition = additions.at(i);
        newRemote.saveDetail(&addition);
    }

    for (int i = 0; i < modifications.size(); ++i) {
        QContactDetail modification = modifications.at(i);
        newRemote.saveDetail(&modification);
    }

    return newRemote;
}


// Determine the delta between prev and curr.
// This function returns two lists:
//   - details in prev which need to removed from UPDATED (copy of prev)
//   - details from curr which need to be added to UPDATED (copy of prev)
// It's not a minimal delta because this function treats modifications as removal+addition.
// We later on attempt to determine which of those removal+addition pairs are "actually" modifications.
QPair<QList<QContactDetail>, QList<QContactDetail> > TwoWayContactSyncAdapter::fallbackDelta(const QContact &prev, const QContact &curr) const
{
    QList<QContactDetail> pdets = prev.details();
    QList<QContactDetail> cdets = curr.details();

    // XXX TODO: handle Unique details (Guid / Name / etc)

    // ignore all exact matches, as they don't form part of the delta.
    for (int i = pdets.size() - 1; i >= 0; --i) {
        int idx = -1;
        for (int j = 0; j < cdets.size(); ++j) {
            if (pdets[i] == cdets[j]) {
                idx = j;
                break;
            }
        }
        if (idx != -1) {
            // found an exact match; this detail hasn't changed.
            pdets.removeAt(i);
            cdets.removeAt(idx);
        }
    }

    // anything which remains is in the delta.
    return QPair<QList<QContactDetail>, QList<QContactDetail> >(pdets, cdets);
}

// Given a list of removals and a list of additions,
// attempt to transform removal+addition pairs into modifications
// if the changes are minimal enough to be considered a modification.
QList<QContactDetail> TwoWayContactSyncAdapter::improveDelta(QList<QContactDetail> *removals, QList<QContactDetail> *additions) const
{
    QList<QContactDetail> finalRemovals;
    QList<QContactDetail> finalAdditions;
    QList<QContactDetail> finalModifications;
    QMultiMap<int, QContactDetail> bucketedRemovals;
    QMultiMap<int, QContactDetail> bucketedAdditions;

    for (int i = 0; i < removals->size(); ++i)
        bucketedRemovals.insertMulti(removals->at(i).type(), removals->at(i));
    for (int i = 0; i < additions->size(); ++i)
        bucketedAdditions.insertMulti(additions->at(i).type(), additions->at(i));

    QSet<int> seenTypes;
    foreach (int type, bucketedRemovals.keys()) {
        seenTypes.insert(type);
        QList<QContactDetail> removalsOfThisType = bucketedRemovals.values(type);
        QList<QContactDetail> additionsOfThisType = bucketedAdditions.values(type);
        QList<QContactDetail> modificationsOfThisType = determineModifications(&removalsOfThisType, &additionsOfThisType);
        finalRemovals.append(removalsOfThisType);
        finalAdditions.append(additionsOfThisType);
        finalModifications.append(modificationsOfThisType);
    }

    foreach (int type, bucketedAdditions.keys()) {
        if (!seenTypes.contains(type)) {
            finalAdditions.append(bucketedAdditions.values(type));
        }
    }

    *removals = finalRemovals;
    *additions = finalAdditions;
    return finalModifications;
}

// Note: this implementation can be overridden if the sync adapter knows
// more about how to determine modifications (eg persistent detail ids)
QList<QContactDetail> TwoWayContactSyncAdapter::determineModifications(QList<QContactDetail> *removalsOfThisType, QList<QContactDetail> *additionsOfThisType) const
{
    QList<QContactDetail> modifications;
    QList<QContactDetail> finalRemovals;
    QList<QContactDetail> finalAdditions;

    QList<QPair<int, int> > permutationsOfIndexes;
    QMap<int, int> scoresForPermutations;

    QList<int> remainingRemovals;
    QList<int> remainingAdditions;

    // for each possible permutation, determine its score.
    // lower is a closer match (ie, score == distance).
    for (int i = 0; i < removalsOfThisType->size(); ++i) {
        for (int j = 0; j < additionsOfThisType->size(); ++j) {
            // determine the score for the permutation
            scoresForPermutations.insert(permutationsOfIndexes.size(),
                                         scoreForDetailPair(removalsOfThisType->at(i),
                                                            additionsOfThisType->at(j)));
            permutationsOfIndexes.append(QPair<int, int>(i, j));

            // this is so that we can avoid "re-using" details in modification pairs.
            if (!remainingRemovals.contains(i)) {
                remainingRemovals.append(i);
            }
            if (!remainingAdditions.contains(j)) {
                remainingAdditions.append(j);
            }
        }
    }

    while (remainingRemovals.size() > 0 && remainingAdditions.size() > 0) {
        int lowScore = 1000;
        int lowScorePermutationIdx = -1;

        foreach (int permutationIdx, scoresForPermutations.keys()) {
            QPair<int, int> permutation = permutationsOfIndexes.at(permutationIdx);
            if (remainingRemovals.contains(permutation.first)
                    && remainingAdditions.contains(permutation.second)) {
                // this permutation is still "possible".
                if (scoresForPermutations.value(permutationIdx) < lowScore) {
                    lowScorePermutationIdx = permutationIdx;
                    lowScore = scoresForPermutations.value(permutationIdx);
                }
            }
        }

        if (lowScorePermutationIdx != -1) {
            // we have a valid permutation which should be treated as a modification.
            QPair<int, int> bestPermutation = permutationsOfIndexes.at(lowScorePermutationIdx);
            remainingRemovals.removeAll(bestPermutation.first);
            remainingAdditions.removeAll(bestPermutation.second);
            QContactDetail modification = removalsOfThisType->at(bestPermutation.first);
            QContactDetail finalValue = additionsOfThisType->at(bestPermutation.second);
            QMap<int, QVariant> values = finalValue.values();
            foreach (int field, values.keys()) {
                modification.setValue(field, values.value(field));
            }
            QMap<int, QVariant> possiblyStaleValues = modification.values();
            foreach (int field, possiblyStaleValues.keys()) {
                if (!values.contains(field)) {
                    modification.removeValue(field);
                }
            }
            modifications.append(modification);
        }
    }

    // rebuild the return values, removing the permutations which were applied as modifications.
    foreach (int idx, remainingRemovals)
        finalRemovals.append(removalsOfThisType->at(idx));
    foreach (int idx, remainingAdditions)
        finalAdditions.append(additionsOfThisType->at(idx));

    // and return.
    *removalsOfThisType = finalRemovals;
    *additionsOfThisType = finalAdditions;
    return modifications;
}

int TwoWayContactSyncAdapter::scoreForValuePair(const QVariant &removal, const QVariant &addition) const
{
    // work around some variant-comparison issues.
    if (Q_UNLIKELY((((removal.type() == QVariant::String && addition.type() == QVariant::Invalid)
                   ||(addition.type() == QVariant::String && removal.type() == QVariant::Invalid))
                   &&(removal.toString().isEmpty() && addition.toString().isEmpty())))) {
        // it could be that "invalid" variant is stored as an empty
        // string in database, if the field is a string field.
        // if so, ignore that - it's not a difference.
        return 0;
    }

    if (removal.canConvert<QList<int> >() && addition.canConvert<QList<int> >()) {
        // direct comparison of QVariant::fromValue<QList<int> > doesn't work
        // so instead, do the conversion and compare them manually.
        QList<int> rlist = removal.value<QList<int> >();
        QList<int> llist = addition.value<QList<int> >();
        return rlist == llist ? 0 : 1;
    }

    // the sync adaptor might return url data as a string.
    if (removal.type() == QVariant::Url && addition.type() == QVariant::String) {
        QUrl rurl = removal.toUrl();
        QUrl aurl = QUrl(addition.toString());
        return rurl == aurl ? 0 : 1;
    } else if (removal.type() == QVariant::String && addition.type() == QVariant::Url) {
        QUrl rurl = QUrl(removal.toString());
        QUrl aurl = addition.toUrl();
        return rurl == aurl ? 0 : 1;
    }

    // normal case.  if they're different, increase the distance.
    return removal == addition ? 0 : 1;
}

int TwoWayContactSyncAdapter::scoreForDetailPair(const QContactDetail &removal, const QContactDetail &addition) const
{
    int score = 0; // distance
    QMap<int, QVariant> rvalues = removal.values();
    QMap<int, QVariant> avalues = addition.values();

    QList<int> seenFields;
    foreach (int field, rvalues.keys()) {
        seenFields.append(field);
        score += scoreForValuePair(rvalues.value(field), avalues.value(field));
    }

    foreach (int field, avalues.keys()) {
        if (seenFields.contains(field)) continue;
        score += scoreForValuePair(rvalues.value(field), avalues.value(field));
    }

    return score;
}

bool TwoWayContactSyncAdapter::detailPairExactlyMatches(const QContactDetail &a, const QContactDetail &b) const
{
    if (a.type() != b.type()) {
        return false;
    }

    // some fields should not be compared.
    QMap<int, QVariant> avalues = a.values();
    avalues.remove(QContactDetail__FieldProvenance);
    avalues.remove(QContactDetail__FieldModifiable);

    QMap<int, QVariant> bvalues = b.values();
    bvalues.remove(QContactDetail__FieldProvenance);
    bvalues.remove(QContactDetail__FieldModifiable);

    if (a.type() == QContactDetail::TypePhoneNumber) {
        avalues.remove(QContactPhoneNumber__FieldNormalizedNumber);
        bvalues.remove(QContactPhoneNumber__FieldNormalizedNumber);
    }

    // now ensure that all values match
    foreach (int akey, avalues.keys()) {
        QVariant avalue = avalues.value(akey);
        if (!bvalues.contains(akey)) {
            // this may still be ok if the avalue is NULL
            // or if the avalue is an empty string,
            // as the database can sometimes return empty
            // string instead of NULL value.
            if ((avalue.type() == QVariant::String && avalue.toString().isEmpty())
                    || avalue.type() == QVariant::Invalid) {
                // this is ok.
            } else {
                // a has a real value which b does not have.
                return false;
            }
        } else {
            // b contains the same key, but do the values match?
            if (scoreForValuePair(avalue, bvalues.value(akey)) != 0) {
                return false;
            }

            // yes, they match.
            bvalues.remove(akey);
        }
    }

    // if there are any non-empty/null values left in b, then
    // a and b do not exactly match.
    foreach (int bkey, bvalues.keys()) {
        QVariant bvalue = bvalues.value(bkey);
        if ((bvalue.type() == QVariant::String && bvalue.toString().isEmpty())
                || bvalue.type() == QVariant::Invalid) {
            // this is ok.
        } else {
            // b has a real value which a does not have.
            return false;
        }
    }

    return true;
}

int TwoWayContactSyncAdapter::exactDetailMatchExistsInList(const QContactDetail &det, const QList<QContactDetail> &list) const
{
    for (int i = 0; i < list.size(); ++i) {
        if (detailPairExactlyMatches(det, list[i])) {
            return i; // exact match at this index.
        }
    }

    return -1;
}

void TwoWayContactSyncAdapter::removeIgnorableDetailsFromList(QList<QContactDetail> *dets) const
{
    // ignore differences in certain detail types
    for (int i = dets->size() - 1; i >= 0; --i) {
        switch (dets->at(i).type()) {
            case QContactDetail::TypeDisplayLabel:   // flow through
            case QContactDetail::TypeGlobalPresence: // flow through
            case QContactDetail::TypePresence:       // flow through
            case QContactDetail::TypeOnlineAccount:  // flow through
            case QContactDetail::TypeSyncTarget:     // flow through
            case QContactDetail::TypeTimestamp:
                dets->removeAt(i);      // we can ignore this detail
                break;
            default: {
                if (dets->at(i).type() == QContactDetail__TypeStatusFlags ||
                    dets->at(i).type() == QContactDetail__TypeDeactivated ||
                    dets->at(i).type() == QContactDetail__TypeIncidental) {
                    // we can ignore this detail
                    dets->removeAt(i);
                }
                break;
            }
        }
    }
}

int TwoWayContactSyncAdapter::exactContactMatchExistsInList(const QContact &c, const QList<QContact> &list) const
{
    QList<QContactDetail> cdets = c.details();
    removeIgnorableDetailsFromList(&cdets);

    for (int i = 0; i < list.size(); ++i) {
        // for it to be an exact match:
        // a) every detail in cdets must exist in ldets
        // b) no extra details can exist in ldets
        QList<QContactDetail> ldets = list[i].details();
        removeIgnorableDetailsFromList(&ldets);
        if (ldets.size() != cdets.size()) {
            continue;
        }

        bool everythingMatches = true;
        foreach (const QContactDetail &d, cdets) {
            int exactMatchIndex = exactDetailMatchExistsInList(d, ldets);
            if (exactMatchIndex == -1) {
                // no exact match for this detail.
                everythingMatches = false;
                break;
            } else {
                // found a match for this detail.
                // remove it from ldets so that duplicates in cdets
                // don't mess up our detection.
                ldets.removeAt(exactMatchIndex);
            }
        }

        if (everythingMatches && ldets.size() == 0) {
            return i; // exact match at this index.
        }
    }

    return -1;
}

void TwoWayContactSyncAdapter::dumpContact(const QContact &c) const
{
    QList<QContactDetail> cdets = c.details();
    removeIgnorableDetailsFromList(&cdets);
    foreach (const QContactDetail &det, cdets) {
        qWarning() << "++ ---------" << det.type();
        QMap<int, QVariant> values = det.values();
        foreach (int key, values.keys()) {
            qWarning() << "    " << key << "=" << values.value(key);
        }
    }
}

#endif

