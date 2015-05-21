/*
 * Copyright (C) 2015 Jolla Ltd.
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

#include "TestSyncAdapter.h"
#include "../../../src/extensions/qtcontacts-extensions_manager_impl.h"
#include "../../../src/extensions/twowaycontactsyncadapter_impl.h"
#include "../../../src/extensions/qtcontacts-extensions.h"

#include <QUuid>

#include <QContact>
#include <QContactFavorite>
#include <QContactName>
#include <QContactEmailAddress>
#include <QContactPhoneNumber>
#include <QContactDisplayLabel>
#include <QContactHobby>
#include <QContactAvatar>
#include <QContactAddress>
#include <QContactPresence>
#include <QContactNickname>
#include <QContactOnlineAccount>
#include <QContactSyncTarget>
#include <QContactGuid>

#define TSA_GUID_STRING(accountId, fname, lname) QString(accountId + ":" + fname + lname)

namespace {

QMap<QString, QString> managerParameters() {
    QMap<QString, QString> params;
    params.insert(QStringLiteral("autoTest"), QStringLiteral("true"));
    params.insert(QStringLiteral("mergePresenceChanges"), QStringLiteral("true"));
    return params;
}

QStringList generateFirstNamesList()
{
    QStringList retn;
    retn << "Alexandria" << "Andrew" << "Adrien" << "Amos"
         << "Bob" << "Bronte" << "Barry" << "Braxton"
         << "Clarence" << "Chandler" << "Chris" << "Chantelle"
         << "Dominic" << "Diedre" << "David" << "Derrick"
         << "Eric" << "Esther" << "Eddie" << "Eean"
         << "Felicity" << "Fred" << "Fletcher" << "Farraday"
         << "Gary" << "Gertrude" << "Gerry" << "Germaine"
         << "Hillary" << "Henry" << "Hans" << "Haddock"
         << "Jacob" << "Jane" << "Jackson" << "Jennifer"
         << "Larry" << "Lilliane" << "Lambert" << "Lilly"
         << "Mary" << "Mark" << "Mirriam" << "Matthew"
         << "Nathene" << "Nicholas" << "Ned" << "Norris"
         << "Othello" << "Oscar" << "Olaf" << "Odinsdottur"
         << "Penny" << "Peter" << "Patrick" << "Pilborough"
         << "Zach" << "Zane" << "Zinedine" << "Zockey"
         << "Yann" << "Yedrez" << "Yarrow" << "Yelter"
         << "Ximmy" << "Xascha" << "Xanthar" << "Xachy"
         << "William" << "Wally" << "Weston" << "Wulther"
         << "Vernon" << "Veston" << "Victoria" << "Vuitton"
         << "Urqhart" << "Uelela" << "Ulrich" << "Umpty"
         << "Timothy" << "Tigga" << "Tabitha" << "Texter"
         << "Stan" << "Steve" << "Sophie" << "Siphonie"
         << "Richard" << "Rafael" << "Rachael" << "Rascal"
         << "Quirky" << "Quilton" << "Quentin" << "Quarreller";
    return retn;
}

QStringList generateMiddleNamesList()
{
    QStringList retn;
    retn << "Aubrey" << "Cody" << "Taylor" << "Leslie";
    return retn;
}

QStringList generateLastNamesList()
{
    QStringList retn;
    retn << "Arkady" << "Addleman" << "Axeman" << "Applegrower" << "Anderson"
         << "Baker" << "Bremmer" << "Bedlam" << "Barrymore" << "Battery"
         << "Cutter" << "Cooper" << "Cutler" << "Catcher" << "Capemaker"
         << "Driller" << "Dyer" << "Diver" << "Daytona" << "Duster"
         << "Eeler" << "Eckhart" << "Eggsman" << "Empty" << "Ellersly"
         << "Farmer" << "Farrier" << "Foster" << "Farseer" << "Fairtime"
         << "Grower" << "Gaston" << "Gerriman" << "Gipsland" << "Guilder"
         << "Helper" << "Hogfarmer" << "Harriet" << "Hope" << "Huxley"
         << "Inker" << "Innman" << "Ipland" << "Instiller" << "Innis"
         << "Joker" << "Jackson" << "Jolt" << "Jockey" << "Jerriman"
         << "Quilter" << "Qualifa" << "Quarrier" << "Quickson"
         << "Rigger" << "Render" << "Ranger" << "Reader"
         << "Sailor" << "Smith" << "Salter" << "Shelfer"
         << "Tailor" << "Tasker" << "Toppler" << "Tipster"
         << "Underhill" << "Umpire" << "Upperhill" << "Uppsland"
         << "Vintner" << "Vester" << "Victor" << "Vacationer"
         << "Wicker" << "Whaler" << "Whistler" << "Wolf"
         << "Xylophone" << "Xabu" << "Xanadu" << "Xatti"
         << "Yeoman" << "Yesman" << "Yelper" << "Yachtsman"
         << "Zimmerman" << "Zomething" << "Zeltic" << "Zephyr";
    return retn;
}

QStringList generatePhoneNumbersList()
{
    QStringList retn;
    retn << "111222" << "111333" << "111444" << "111555" << "111666"
         << "111777" << "111888" << "111999" << "222333" << "222444"
         << "222555" << "222666" << "222777" << "222888" << "222999"
         << "333444" << "333555" << "333666" << "333777" << "333888"
         << "333999" << "444555" << "444666" << "444777" << "444888"
         << "444999" << "555666" << "555777" << "555888" << "555999"
         << "666111" << "666222" << "666333" << "666444" << "666555"
         << "777111" << "777222" << "777333" << "777444" << "777555"
         << "777666" << "888111" << "888222" << "888333" << "888444"
         << "888555" << "888666" << "888777" << "999111" << "999222"
         << "999333" << "999444" << "999555" << "999666" << "999777"
         << "999888" << "999999";
    return retn;
}

QStringList generateEmailProvidersList()
{
    QStringList retn;
    retn << "@test.com" << "@testing.com" << "@testers.com"
         << "@test.org" << "@testing.org" << "@testers.org"
         << "@test.net" << "@testing.net" << "@testers.net"
         << "@test.fi" << "@testing.fi" << "@testers.fi"
         << "@test.com.au" << "@testing.com.au" << "@testers.com.au"
         << "@test.co.uk" << "@testing.co.uk" << "@testers.co.uk"
         << "@test.co.jp" << "@test.co.jp" << "@testers.co.jp";
    return retn;
}

QStringList generateAvatarsList()
{
    QStringList retn;
    retn << "-smiling.jpg" << "-laughing.jpg" << "-surprised.jpg"
         << "-smiling.png" << "-laughing.png" << "-surprised.png"
         << "-curious.jpg" << "-joking.jpg" << "-grinning.jpg"
         << "-curious.png" << "-joking.png" << "-grinning.png";
    return retn;
}

QStringList generateHobbiesList()
{
    QStringList retn;
    retn << "tennis" << "soccer" << "squash" << "volleyball"
         << "chess" << "photography" << "painting" << "sketching";
    return retn;
}

QContact generateContact(const QString &syncTarget, int which)
{
    static const QStringList firstNames(generateFirstNamesList());
    static const QStringList middleNames(generateMiddleNamesList());
    static const QStringList lastNames(generateLastNamesList());
    static const QStringList phoneNumbers(generatePhoneNumbersList());
    static const QStringList emailProviders(generateEmailProvidersList());
    static const QStringList avatars(generateAvatarsList());
    static const QStringList hobbies(generateHobbiesList());

    // we randomly determine whether to generate various details
    // to ensure that we have heterogeneous contacts in the db.
    QContact retn;
    int random = qrand();

    // We always have a sync target.
    QContactSyncTarget synctarget;
    synctarget.setSyncTarget(syncTarget);
    retn.saveDetail(&synctarget);

    // We always have a guid.
    QContactGuid guid;
    guid.setGuid(QUuid::createUuid().toString());
    retn.saveDetail(&guid);

    // We always have a name.  We are generating unique names based on the "which" param.
    int tempWhich = which;
    int whichFirst = tempWhich;
    int whichLast = 0;
    while (tempWhich >= firstNames.size()) {
        tempWhich -= firstNames.size();
        whichFirst = tempWhich;
        whichLast += 1;
    }

    QContactName name;
    if (whichLast >= lastNames.size()) {
        int wrapCount = 0;
        while (whichLast >= lastNames.size()) {
            wrapCount += 1;
            whichLast -= lastNames.size();
        }
        // we have to re-use a name... but we don't want aggregates, so append some garbage to avoid that.
        name.setFirstName(QStringLiteral("%1#%2").arg(firstNames.at(whichFirst)).arg(wrapCount));
        name.setLastName(QStringLiteral("%1#%2").arg(lastNames.at(whichLast)).arg(wrapCount));
    } else {
        name.setFirstName(firstNames.at(whichFirst));
        name.setLastName(lastNames.at(whichLast));
    }
    if ((random % 6) == 0) name.setMiddleName(middleNames.at(random % middleNames.size()));
    if ((random % 17) == 0) name.setPrefix(QLatin1String("Dr."));
    retn.saveDetail(&name);

    // Favorite
    if ((random % 31) == 0) {
        QContactFavorite fav;
        fav.setFavorite(true);
        retn.saveDetail(&fav);
    }

    // Phone number
    if ((random % 3) == 0) {
        QContactPhoneNumber phn;
        QString randomPhn = phoneNumbers.at(random % phoneNumbers.size());
        phn.setNumber(randomPhn);
        if ((random % 9) == 0) phn.setContexts(QContactDetail::ContextWork);
        retn.saveDetail(&phn);
    }

    // Email
    if ((random % 2) == 0) {
        QContactEmailAddress em;
        em.setEmailAddress(QString(QLatin1String("%1%2%3"))
                .arg(name.firstName())
                .arg(name.lastName())
                .arg(emailProviders.at(random % emailProviders.size())));
        if (random % 9) em.setContexts(QContactDetail::ContextWork);
        retn.saveDetail(&em);
    }

    // Avatar
    if ((random % 5) == 0) {
        QContactAvatar av;
        av.setImageUrl(name.firstName() + avatars.at(random % avatars.size()));
        retn.saveDetail(&av);
    }

    // Hobby
    if ((random % 21) == 0) {
        QContactHobby h1;
        h1.setHobby(hobbies.at(random % hobbies.size()));
        retn.saveDetail(&h1);

        int newRandom = qrand();
        if ((newRandom % 2) == 0) {
            QContactHobby h2;
            h2.setHobby(hobbies.at(newRandom % hobbies.size()));
            retn.saveDetail(&h2);
        }
    }

    return retn;
}

}

TestSyncAdapter::TestSyncAdapter(const QString &accountId, QObject *parent)
    : QObject(parent), TwoWayContactSyncAdapter(QStringLiteral("testsyncadapter"), managerParameters())
    , m_accountId(accountId), m_step(0)
{
    cleanUp();
    generateInitialContactsPool();
}

TestSyncAdapter::~TestSyncAdapter()
{
    cleanUp();
}

void TestSyncAdapter::cleanUp()
{
    initSyncAdapter(m_accountId);
    readSyncStateData(&m_remoteSince, m_accountId, TwoWayContactSyncAdapter::ReadPartialState);
    purgeSyncStateData(m_accountId, true);
    removeAllContacts();
}

void TestSyncAdapter::performTwoWaySync()
{
    // each time we trigger two way sync, we increment our "step" counter.
    // we generate different remote changes (addmods/deletes) based on the step count.
    m_step += 1;

    if (!initSyncAdapter(m_accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't init adapter";
        emit failed();
        return;
    }

    if (!readSyncStateData(&m_remoteSince, m_accountId, TwoWayContactSyncAdapter::ReadPartialState)) {
        qWarning() << Q_FUNC_INFO << "couldn't read sync state data";
        emit failed();
        return;
    }

    determineRemoteChanges(m_remoteSince, m_accountId);
}

void TestSyncAdapter::determineRemoteChanges(const QDateTime &, const QString &accountId)
{
    // generate remote changes for this step / sync cycle.
    QList<QContact> remoteAddMods;
    QList<QContact> remoteDeletions;
    switch (m_step) {
    case 1: {
        // the first 3000 contacts are additions.
        // there are no modifications or deletions because this is the first sync.
        for (int i = 0; i < 3000; ++i) remoteAddMods.append(m_initialPool.takeFirst());
    } break;
    case 2: {
        // the next 2000 contacts from the initial pool are additions.
        // we also do the step-three generation as well.
        remoteAddMods = m_initialPool; m_initialPool.clear();
    } // flow on
    case 3: {
        // we modify the first thousand contacts which were previously added and report them as modifications.
        // we report the last thousand contacts which were previously added and report them as deletions.
        QStringList serverContactGuids = m_serverContacts.keys();
        for (int i = 0; i < 1000; ++i) {
            QContact modC = m_serverContacts[serverContactGuids[i]];
            QContactName modName = modC.detail<QContactName>();
            modName.setFirstName(modName.firstName() + QStringLiteral("Mod"));
            modC.saveDetail(&modName);
            remoteAddMods.append(modC);
        }
        for (int i = 1; i < 1001; ++i) {
            QContact delC = m_serverContacts[serverContactGuids[serverContactGuids.size() - i]];
            remoteDeletions.append(delC);
        }
    } break;
    default: qWarning() << "Invalid step:" << m_step << ", reporting no remote changes this sync cycle";
    }

    // store those remote changes locally via TWCSA
    if (!storeRemoteChanges(remoteDeletions, &remoteAddMods, accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't store remote changes";
        emit failed();
        return;
    }

    // Update the server-side version of every contact we added/modified (including ID)
    Q_FOREACH (const QContact &c, remoteAddMods) {
        m_serverContacts.insert(c.detail<QContactGuid>().guid(), c);
    }
    // Delete the server-side version of every contact we deleted
    Q_FOREACH (const QContact &c, remoteDeletions) {
        m_serverContacts.remove(c.detail<QContactGuid>().guid());
    }

    // Now continue the sync cycle - determine local changes and "upsync them"
    QList<QContact> locallyAdded, locallyModified, locallyDeleted;
    QDateTime localSince;
    if (!determineLocalChanges(&localSince, &locallyAdded, &locallyModified, &locallyDeleted, accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't determine local changes";
        emit failed();
        return;
    }

    upsyncLocalChanges(localSince, locallyAdded, locallyModified, locallyDeleted, accountId);
}

void TestSyncAdapter::upsyncLocalChanges(const QDateTime &,
                                         const QList<QContact> &locallyAdded,
                                         const QList<QContact> &locallyModified,
                                         const QList<QContact> &locallyDeleted,
                                         const QString &accountId)
{
    // apply the local changes to our in memory store.
    foreach (const QContact &c, locallyAdded + locallyModified) {
        m_serverContacts.insert(c.detail<QContactGuid>().guid(), c);
    }
    foreach (const QContact &c, locallyDeleted) {
        m_serverContacts.remove(c.detail<QContactGuid>().guid());
    }

    // successfully completed a simulated sync cycle.
    if (!storeSyncStateData(accountId)) {
        qWarning() << Q_FUNC_INFO << "couldn't store sync state data";
        emit failed();
        return;
    }
    emit finished(); // succeeded.
}

bool TestSyncAdapter::testAccountProvenance(const QContact &contact, const QString &)
{
    return m_serverContacts.contains(contact.detail<QContactGuid>().guid());
}

void TestSyncAdapter::generateInitialContactsPool()
{
    m_initialPool.reserve(5000);
    for (int i = 0; i < 5000; ++i) {
        m_initialPool.append(generateContact(QStringLiteral("testing-%1").arg(m_accountId), i));
    }
}
