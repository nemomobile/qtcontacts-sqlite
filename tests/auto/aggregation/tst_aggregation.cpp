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

#define QT_STATICPLUGIN

#include <QtTest/QtTest>
#include <QtGlobal>
#include <QtCore/qnumeric.h>

#include "qtcontacts.h"
#include "qcontactchangeset.h"

QTM_USE_NAMESPACE
// Eventually these will make it into qtestcase.h
// but we might need to tweak the timeout values here.
#ifndef QTRY_COMPARE
#define QTRY_COMPARE(__expr, __expected) \
    do { \
        const int __step = 50; \
        const int __timeout = 5000; \
        if ((__expr) != (__expected)) { \
            QTest::qWait(0); \
        } \
        for (int __i = 0; __i < __timeout && ((__expr) != (__expected)); __i+=__step) { \
            QTest::qWait(__step); \
        } \
        QCOMPARE(__expr, __expected); \
    } while(0)
#endif

#ifndef QTRY_VERIFY
#define QTRY_VERIFY(__expr) \
        do { \
        const int __step = 50; \
        const int __timeout = 5000; \
        if (!(__expr)) { \
            QTest::qWait(0); \
        } \
        for (int __i = 0; __i < __timeout && !(__expr); __i+=__step) { \
            QTest::qWait(__step); \
        } \
        QVERIFY(__expr); \
    } while(0)
#endif


#define QTRY_WAIT(code, __expr) \
        do { \
        const int __step = 50; \
        const int __timeout = 5000; \
        if (!(__expr)) { \
            QTest::qWait(0); \
        } \
        for (int __i = 0; __i < __timeout && !(__expr); __i+=__step) { \
            do { code } while(0); \
            QTest::qWait(__step); \
        } \
    } while(0)

#define QTRY_COMPARE_SIGNALS_LOCALID_COUNT(__signalSpy, __expectedCount) \
    do { \
        int __spiedSigCount = 0; \
        const int __step = 50; \
        const int __timeout = 5000; \
        for (int __i = 0; __i < __timeout; __i+=__step) { \
            /* accumulate added from signals */ \
            __spiedSigCount = 0; \
            const QList<QList<QVariant> > __spiedSignals = __signalSpy; \
            foreach (const QList<QVariant> &__arguments, __spiedSignals) { \
                foreach (QContactLocalId __localId, __arguments.first().value<QList<QContactLocalId> >()) { \
                    QVERIFY(__localId!=0); \
                    __spiedSigCount++; \
                } \
            } \
            if(__spiedSigCount == __expectedCount) { \
                break; \
            } \
            QTest::qWait(__step); \
        } \
        QCOMPARE(__spiedSigCount, __expectedCount); \
    } while(0)

class tst_Aggregation : public QObject
{
    Q_OBJECT

public:
    tst_Aggregation();
    virtual ~tst_Aggregation();

public slots:
    void initTestCase();
    void cleanupTestCase();

public slots:
    void addAccumulationSlot(const QList<QContactLocalId> &ids);
    void chgAccumulationSlot(const QList<QContactLocalId> &ids);
    void remAccumulationSlot(const QList<QContactLocalId> &ids);

private slots:
    void createSingleLocal();
    void createMultipleLocal();
    void createSingleLocalAndSingleSync();

    void updateSingleLocal();
    void updateSingleAggregate();

    void removeSingleLocal();
    void removeSingleAggregate();

    void regenerateAggregate();

    void detailUris();

    void correctDetails();

    void customSemantics();

private:
    QContactManager *m_cm;
    QList<QContactLocalId> m_addAccumulatedIds;
    QList<QContactLocalId> m_chgAccumulatedIds;
    QList<QContactLocalId> m_remAccumulatedIds;
};

tst_Aggregation::tst_Aggregation()
    : m_cm(new QContactManager(QLatin1String("org.nemomobile.contacts.sqlite")))
{
    QTest::qWait(250); // creating self contact etc will cause some signals to be emitted.  ignore them.
    connect(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)), this, SLOT(addAccumulationSlot(QList<QContactLocalId>)));
    connect(m_cm, SIGNAL(contactsChanged(QList<QContactLocalId>)), this, SLOT(chgAccumulationSlot(QList<QContactLocalId>)));
    connect(m_cm, SIGNAL(contactsRemoved(QList<QContactLocalId>)), this, SLOT(remAccumulationSlot(QList<QContactLocalId>)));
}

tst_Aggregation::~tst_Aggregation()
{
}

void tst_Aggregation::initTestCase()
{
}

void tst_Aggregation::cleanupTestCase()
{
}

void tst_Aggregation::addAccumulationSlot(const QList<QContactLocalId> &ids)
{
    m_addAccumulatedIds.append(ids);
}

void tst_Aggregation::chgAccumulationSlot(const QList<QContactLocalId> &ids)
{
    m_chgAccumulatedIds.append(ids);
}

void tst_Aggregation::remAccumulationSlot(const QList<QContactLocalId> &ids)
{
    m_remAccumulatedIds.append(ids);
}

void tst_Aggregation::createSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    int addSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("1234567");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("1234567")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else {
            qWarning() << "Found unrelated contact:"
                       << currSt.syncTarget()
                       << currName.firstName()
                       << currName.middleName()
                       << currName.lastName()
                       << currPhn.number();
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << aggregateAlice.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::createMultipleLocal()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    int addSpyCount = 0;

    // now add two new local contacts (no synctarget specified == automatically local)
    QContact alice;
    QContact bob;

    QContactName an, bn;
    an.setFirstName("Alice2");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);
    bn.setFirstName("Bob2");
    bn.setMiddleName("The");
    bn.setLastName("Destroyer");
    bob.saveDetail(&bn);

    QContactPhoneNumber aph, bph;
    aph.setNumber("234567");
    alice.saveDetail(&aph);
    bph.setNumber("765432");
    bob.saveDetail(&bph);

    QList<QContact> saveList;
    saveList << alice << bob;
    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContacts(&saveList));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate for each
    alice = saveList.at(0); bob = saveList.at(1);
    QCOMPARE(m_addAccumulatedIds.size(), 4);
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    QVERIFY(m_addAccumulatedIds.contains(bob.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 2); // 2 extra aggregate contacts
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 4); // should have added local + aggregate for each
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    bool foundLocalBob = false;
    bool foundAggregateBob = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice2")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("234567")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else if (currName.firstName() == QLatin1String("Bob2")
                && currName.middleName() == QLatin1String("The")
                && currName.lastName() == QLatin1String("Destroyer")
                && currPhn.number() == QLatin1String("765432")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localBob = curr;
                foundLocalBob = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
                foundAggregateBob = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(foundLocalBob);
    QVERIFY(foundAggregateBob);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(!localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateBob.id()));
    QVERIFY(!aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localBob.id()));
    QVERIFY(localBob.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateBob.id()));
    QVERIFY(aggregateBob.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localBob.id()));
    QVERIFY(!localBob.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(!aggregateBob.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << aggregateAlice.id().localId() << localBob.id().localId() << aggregateBob.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::createSingleLocalAndSingleSync()
{
    // here we create a local contact, and then save it
    // and then we create a "sync" contact, which should "match" it.
    // It should be related to the aggregate created for the sync.

    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    QSignalSpy chgSpy(m_cm, SIGNAL(contactsChanged(QList<QContactLocalId>)));
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice3");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("34567");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("alice@test.com");
    alice.saveDetail(&aem);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate
    QCOMPARE(m_addAccumulatedIds.size(), 2);
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice3")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    // now add the doppleganger from another sync source
    QContact syncAlice;
    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    syncAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber(aph.number());
    syncAlice.saveDetail(&saph);

    QContactEmailAddress saem;
    saem.setEmailAddress(aem.emailAddress());
    syncAlice.saveDetail(&saem);

    QContactHobby sah; // this is a "new" detail which doesn't appear in the local contact.
    sah.setHobby(QLatin1String("tennis"));
    syncAlice.saveDetail(&sah);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    // DON'T clear the m_addAccumulatedIds list here.
    // DO clear the m_chgAccumulatedIds list here, though.
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&syncAlice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added test but not an aggregate - aggregate already exists
    QTRY_VERIFY(chgSpy.count() > chgSpyCount); // should have updated the aggregate
    QCOMPARE(m_addAccumulatedIds.size(), 3);
    QCOMPARE(m_chgAccumulatedIds.size(), 1); // the aggregate should have been updated (with the hobby)
    QVERIFY(m_addAccumulatedIds.contains(localAlice.id().localId()));
    QVERIFY(m_addAccumulatedIds.contains(aggregateAlice.id().localId()));
    QVERIFY(m_addAccumulatedIds.contains(syncAlice.id().localId()));
    QVERIFY(m_chgAccumulatedIds.contains(aggregateAlice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount); // no extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 1); // should have added test but not an aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice3")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QString()); // local shouldn't get it
                localAlice = curr;
                foundLocalAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("test")) {
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(testAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(testAlice.id()));

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << syncAlice.id().localId() << aggregateAlice.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::updateSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    QSignalSpy chgSpy(m_cm, SIGNAL(contactsChanged(QList<QContactLocalId>)));
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("4567");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("4567")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else {
            qWarning() << "Found unrelated contact:"
                       << currSt.syncTarget()
                       << currName.firstName()
                       << currName.middleName()
                       << currName.lastName()
                       << currPhn.number();
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    // now update alice.  The aggregate should get updated also.
    QContactEmailAddress ae;
    ae.setEmailAddress("alice4@test.com");
    QVERIFY(localAlice.saveDetail(&ae));
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&localAlice));
    QTRY_VERIFY(chgSpy.count() > chgSpyCount);
    QTRY_VERIFY(m_chgAccumulatedIds.contains(localAlice.id().localId()));
    QTRY_VERIFY(m_chgAccumulatedIds.contains(aggregateAlice.id().localId()));

    // reload them, and compare.
    localAlice = m_cm->contact(localAlice.id().localId());
    aggregateAlice = m_cm->contact(aggregateAlice.id().localId());
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice4@test.com"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice4@test.com"));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber), QLatin1String("4567"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber), QLatin1String("4567"));

    // now do an update with a definition mask.  We need to be certain that no details were lost.
    ae = localAlice.detail<QContactEmailAddress>();
    ae.setEmailAddress("alice4@test4.com");
    QVERIFY(localAlice.saveDetail(&ae));
    QList<QContact> saveList;
    saveList << localAlice;
    QVERIFY(m_cm->saveContacts(&saveList, QStringList() << QContactEmailAddress::DefinitionName));

    // reload them, and compare.
    localAlice = m_cm->contact(localAlice.id().localId());
    aggregateAlice = m_cm->contact(aggregateAlice.id().localId());
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice4@test4.com"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice4@test4.com"));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber), QLatin1String("4567"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber), QLatin1String("4567"));

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << aggregateAlice.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::updateSingleAggregate()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    QSignalSpy chgSpy(m_cm, SIGNAL(contactsChanged(QList<QContactLocalId>)));
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("567");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("567")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else {
            qWarning() << "Found unrelated contact:"
                       << currSt.syncTarget()
                       << currName.firstName()
                       << currName.middleName()
                       << currName.lastName()
                       << currPhn.number();
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    // now update aggregate alice.  We expect the changes to "down promoted" to the local contact!
    QContactEmailAddress ae;
    ae.setEmailAddress("alice5@test.com");
    aggregateAlice.saveDetail(&ae);
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    QTRY_VERIFY(chgSpy.count() > chgSpyCount);
    QTRY_VERIFY(m_chgAccumulatedIds.contains(localAlice.id().localId()));
    QTRY_VERIFY(m_chgAccumulatedIds.contains(aggregateAlice.id().localId()));

    // reload them, and compare.
    localAlice = m_cm->contact(localAlice.id().localId());
    aggregateAlice = m_cm->contact(aggregateAlice.id().localId());
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice5@test.com"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice5@test.com"));

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << aggregateAlice.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::removeSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    QSignalSpy remSpy(m_cm, SIGNAL(contactsRemoved(QList<QContactLocalId>)));
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("67");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("67")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else {
            qWarning() << "Found unrelated contact:"
                       << currSt.syncTarget()
                       << currName.firstName()
                       << currName.middleName()
                       << currName.lastName()
                       << currPhn.number();
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    // now add another local contact.
    QContact bob;
    QContactName bn;
    bn.setFirstName("Bob7");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    QContactPhoneNumber bp;
    bp.setNumber("777");
    bob.saveDetail(&bn);
    bob.saveDetail(&bp);
    QVERIFY(m_cm->saveContact(&bob));

    // we should have an extra aggregate (bob's) now too
    aggCount = m_cm->contactIds().size();

    // now remove local alice.  We expect that the "orphan" aggregate alice will also be removed.
    remSpyCount = remSpy.count();
    m_remAccumulatedIds.clear();
    QVERIFY(m_cm->removeContact(localAlice.id().localId()));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(localAlice.id().localId()));
    QTRY_VERIFY(m_remAccumulatedIds.contains(aggregateAlice.id().localId()));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // but bob should not have been removed.
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(bob.id().localId()));
    QList<QContact> stillExisting = m_cm->contacts(allSyncTargets);
    bool foundBob = false;
    foreach (const QContact &c, stillExisting) {
        if (c.id().localId() == bob.id().localId()) {
            foundBob = true;
            break;
        }
    }
    QVERIFY(foundBob);

    // now remove bob.
    QVERIFY(m_cm->removeContact(bob.id().localId()));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(bob.id().localId()));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allSyncTargets).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::removeSingleAggregate()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    QSignalSpy remSpy(m_cm, SIGNAL(contactsRemoved(QList<QContactLocalId>)));
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("7");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("7")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else {
            qWarning() << "Found unrelated contact:"
                       << currSt.syncTarget()
                       << currName.firstName()
                       << currName.middleName()
                       << currName.lastName()
                       << currPhn.number();
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    // now add another local contact.
    QContact bob;
    QContactName bn;
    bn.setFirstName("Bob7");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    QContactPhoneNumber bp;
    bp.setNumber("777");
    bob.saveDetail(&bn);
    bob.saveDetail(&bp);
    QVERIFY(m_cm->saveContact(&bob));

    // we should have an extra aggregate (bob's) now too
    aggCount = m_cm->contactIds().size();

    // now remove aggregate alice.  We expect that the "orphan" local alice will also be removed.
    remSpyCount = remSpy.count();
    m_remAccumulatedIds.clear();
    QVERIFY(m_cm->removeContact(aggregateAlice.id().localId()));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(localAlice.id().localId()));
    QTRY_VERIFY(m_remAccumulatedIds.contains(aggregateAlice.id().localId()));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // but bob should not have been removed.
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(bob.id().localId()));
    QList<QContact> stillExisting = m_cm->contacts(allSyncTargets);
    bool foundBob = false;
    foreach (const QContact &c, stillExisting) {
        if (c.id().localId() == bob.id().localId()) {
            foundBob = true;
            break;
        }
    }
    QVERIFY(foundBob);

    // now remove bob.
    QVERIFY(m_cm->removeContact(bob.id().localId()));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(bob.id().localId()));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allSyncTargets).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::regenerateAggregate()
{
    // here we create a local contact, and then save it
    // and then we create a "sync" contact, which should "match" it.
    // It should be related to the aggregate created for the sync.
    // We then remove the sync target, which should cause the aggregate
    // to be "regenerated" from the remaining aggregated contacts
    // (which in this case, is just the local contact).

    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, SIGNAL(contactsAdded(QList<QContactLocalId>)));
    QSignalSpy chgSpy(m_cm, SIGNAL(contactsChanged(QList<QContactLocalId>)));
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice8");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("88888");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("alice8@test.com");
    alice.saveDetail(&aem);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate
    QCOMPARE(m_addAccumulatedIds.size(), 2);
    QVERIFY(m_addAccumulatedIds.contains(alice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));

    // now add the doppleganger from another sync source
    QContact syncAlice;
    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    syncAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber(aph.number());
    syncAlice.saveDetail(&saph);

    QContactEmailAddress saem;
    saem.setEmailAddress(aem.emailAddress());
    syncAlice.saveDetail(&saem);

    QContactHobby sah; // this is a "new" detail which doesn't appear in the local contact.
    sah.setHobby(QLatin1String("tennis"));
    syncAlice.saveDetail(&sah);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    // DON'T clear the m_addAccumulatedIds list here.
    // DO clear the m_chgAccumulatedIds list here, though.
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&syncAlice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added test but not an aggregate - aggregate already exists
    QTRY_VERIFY(chgSpy.count() > chgSpyCount); // should have updated the aggregate
    QCOMPARE(m_addAccumulatedIds.size(), 3);
    QCOMPARE(m_chgAccumulatedIds.size(), 1); // the aggregate should have been updated (with the hobby)
    QVERIFY(m_addAccumulatedIds.contains(localAlice.id().localId()));
    QVERIFY(m_addAccumulatedIds.contains(aggregateAlice.id().localId()));
    QVERIFY(m_addAccumulatedIds.contains(syncAlice.id().localId()));
    QVERIFY(m_chgAccumulatedIds.contains(aggregateAlice.id().localId()));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount); // no extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 1); // should have added test but not an aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QString()); // local shouldn't get it
                localAlice = curr;
                foundLocalAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("test")) {
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(testAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(QContactRelationship::Aggregates, QContactRelationship::Second).contains(testAlice.id()));

    // now remove the "test" sync contact
    QVERIFY(m_cm->removeContact(testAlice.id().localId()));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(testAlice.id().localId())); // should have been removed

    // but the other contacts should NOT have been removed
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(localAlice.id().localId()));
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(aggregateAlice.id().localId()));

    // reload them, and ensure that the "hobby" detail has been removed from the aggregate
    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QString());
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby), QString());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << syncAlice.id().localId() << aggregateAlice.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::detailUris()
{
    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);

    // save alice.  Some details will have a detailUri or linkedDetailUris
    QContact alice;
    QContactName an;
    an.setFirstName("Alice9");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);
    QContactPhoneNumber aph;
    aph.setNumber("99999");
    aph.setDetailUri("alice9PhoneNumberDetailUri");
    alice.saveDetail(&aph);
    QContactEmailAddress aem;
    aem.setEmailAddress("alice9@test.com");
    aem.setLinkedDetailUris("alice9PhoneNumberDetailUri");
    alice.saveDetail(&aem);
    QVERIFY(m_cm->saveContact(&alice));

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice9")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("99999")
                && currEm.emailAddress() == QLatin1String("alice9@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);

    // now check to ensure that the detail uris and links were updated correctly
    // in the aggregate.  Those uris need to be unique in the database.
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().detailUri(), QLatin1String("alice9PhoneNumberDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().detailUri(), QLatin1String("aggregate:alice9PhoneNumberDetailUri"));
    QCOMPARE(localAlice.detail<QContactEmailAddress>().linkedDetailUris(), QStringList() << QLatin1String("alice9PhoneNumberDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris(), QStringList() << QLatin1String("aggregate:alice9PhoneNumberDetailUri"));

    // now perform an update of the local contact.  This should also trigger regeneration of the aggregate.
    QContactHobby ah;
    ah.setHobby("tennis");
    ah.setDetailUri("alice9HobbyDetailUri");
    localAlice.saveDetail(&ah);
    QVERIFY(m_cm->saveContact(&localAlice));

    // reload them both
    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice9")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("99999")
                && currEm.emailAddress() == QLatin1String("alice9@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);

    // now check to ensure that the detail uris and links were updated correctly
    // in the aggregate.  Those uris need to be unique in the database.
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().detailUri(), QLatin1String("alice9PhoneNumberDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().detailUri(), QLatin1String("aggregate:alice9PhoneNumberDetailUri"));
    QCOMPARE(localAlice.detail<QContactEmailAddress>().linkedDetailUris(), QStringList() << QLatin1String("alice9PhoneNumberDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris(), QStringList() << QLatin1String("aggregate:alice9PhoneNumberDetailUri"));
    QCOMPARE(localAlice.detail<QContactHobby>().detailUri(), QLatin1String("alice9HobbyDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactHobby>().detailUri(), QLatin1String("aggregate:alice9HobbyDetailUri"));

    QList<QContactLocalId> removeList;
    removeList << localAlice.id().localId() << aggregateAlice.id().localId();
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::correctDetails()
{
    QContact a, b, c, d;
    QContactName an, bn, cn, dn;
    QContactPhoneNumber ap, bp, cp, dp;
    QContactEmailAddress ae, be, ce, de;
    QContactHobby ah, bh, ch, dh;

    an.setFirstName("a"); an.setLastName("A");
    bn.setFirstName("b"); bn.setLastName("B");
    cn.setFirstName("c"); cn.setLastName("C");
    dn.setFirstName("d"); dn.setLastName("D");

    ap.setNumber("123");
    bp.setNumber("234");
    cp.setNumber("345");
    dp.setNumber("456");

    ae.setEmailAddress("a@test.com");
    be.setEmailAddress("b@test.com");
    ce.setEmailAddress("c@test.com");
    de.setEmailAddress("d@test.com");

    ah.setHobby("soccer");
    bh.setHobby("tennis");
    ch.setHobby("squash");

    a.saveDetail(&an); a.saveDetail(&ap); a.saveDetail(&ae); a.saveDetail(&ah);
    b.saveDetail(&bn); b.saveDetail(&bp); b.saveDetail(&be); b.saveDetail(&bh);
    c.saveDetail(&cn); c.saveDetail(&cp); c.saveDetail(&ce); c.saveDetail(&ch);
    d.saveDetail(&dn); d.saveDetail(&dp); d.saveDetail(&de);

    QList<QContact> saveList;
    saveList << a << b << c << d;
    m_cm->saveContacts(&saveList);

    QContactDetailFilter allSyncTargets;
    allSyncTargets.setDetailDefinitionName(QContactSyncTarget::DefinitionName, QContactSyncTarget::FieldSyncTarget);
    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);

    QVERIFY(allContacts.size() >= saveList.size()); // at least that amount, maybe more (aggregates)
    for (int i = 0; i < allContacts.size(); ++i) {
        QContact curr = allContacts.at(i);
        bool needsComparison = true;
        QContact xpct;
        if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
               a.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = a;
        } else if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
                      b.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = b;
        } else if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
                      c.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = c;
        } else if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
                      d.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = d;
        } else {
            needsComparison = false;
        }

        if (needsComparison) {
            //qWarning() << "actual:" << i
            //           << curr.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget)
            //           << curr.detail<QContactName>().value(QContactName::FieldFirstName)
            //           << curr.detail<QContactName>().value(QContactName::FieldLastName)
            //           << curr.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber)
            //           << curr.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress)
            //           << curr.detail<QContactHobby>().value(QContactHobby::FieldHobby);
            //qWarning() << "expected:" << i
            //           << xpct.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget)
            //           << xpct.detail<QContactName>().value(QContactName::FieldFirstName)
            //           << xpct.detail<QContactName>().value(QContactName::FieldLastName)
            //           << xpct.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber)
            //           << xpct.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress)
            //           << xpct.detail<QContactHobby>().value(QContactHobby::FieldHobby);
            QCOMPARE(curr.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber),
                     xpct.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber));
            QCOMPARE(curr.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress),
                     xpct.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress));
            QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby),
                     xpct.detail<QContactHobby>().value(QContactHobby::FieldHobby));
        }
    }

    QList<QContactLocalId> removeList;
    foreach (const QContact &doomed, allContacts) {
        removeList.append(doomed.id().localId());
    }
    m_cm->removeContacts(removeList);
    QTest::qWait(500); // coalesced signals
}

void tst_Aggregation::customSemantics()
{
    // the qtcontacts-sqlite engine defines some custom semantics
    // 1) avatars have a custom "AvatarMetadata" field
    // 2) self contact cannot be changed, and its id will always be "2"

    // 1 - ensure that the AvatarMetadata field is supported.
    QContact alice;
    QContactName an;
    an.setFirstName("Alice");
    alice.saveDetail(&an);
    QContactAvatar aa;
    aa.setImageUrl(QUrl(QString(QLatin1String("test.png"))));
    aa.setValue(QLatin1String("AvatarMetadata"), "cover");
    alice.saveDetail(&aa);
    QVERIFY(m_cm->saveContact(&alice));
    QContact aliceReloaded = m_cm->contact(alice.id().localId());
    QCOMPARE(aliceReloaded.detail<QContactName>().value(QContactName::FieldFirstName), QLatin1String("Alice"));
    QCOMPARE(QUrl(aliceReloaded.detail<QContactAvatar>().value(QContactAvatar::FieldImageUrl)).toString(), QUrl(QString(QLatin1String("test.png"))).toString());
    QCOMPARE(aliceReloaded.detail<QContactAvatar>().value(QLatin1String("AvatarMetadata")), QLatin1String("cover"));

    // 2 - test the self contact semantics
    QCOMPARE(m_cm->selfContactId(), QContactLocalId(3));
    QVERIFY(!m_cm->setSelfContactId(alice.id().localId()));

    // cleanup.
    m_cm->removeContact(alice.id().localId());
}

QTEST_MAIN(tst_Aggregation)
#include "tst_aggregation.moc"
