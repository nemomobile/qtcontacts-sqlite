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

#include "../../util.h"

static const QString aggregatesRelationship(relationshipString(QContactRelationship::Aggregates));

namespace {

#ifdef USING_QTPIM
static const char *addedAccumulationSlot = SLOT(addAccumulationSlot(QList<QContactId>));
static const char *changedAccumulationSlot = SLOT(chgAccumulationSlot(QList<QContactId>));
static const char *removedAccumulationSlot = SLOT(remAccumulationSlot(QList<QContactId>));
#else
static const char *addedAccumulationSlot = SLOT(addAccumulationSlot(QList<QContactLocalId>));
static const char *changedAccumulationSlot = SLOT(chgAccumulationSlot(QList<QContactLocalId>));
static const char *removedAccumulationSlot = SLOT(remAccumulationSlot(QList<QContactLocalId>));
#endif

QString detailProvenance(const QContactDetail &detail)
{
    return detail.value<QString>(QContactDetail__FieldProvenance);
}

QString detailProvenanceContact(const QContactDetail &detail)
{
    // The contact element is the first part up to ':'
    const QString provenance(detailProvenance(detail));
    return provenance.left(provenance.indexOf(QChar::fromLatin1(':')));
}

}

class tst_Aggregation : public QObject
{
    Q_OBJECT

public:
    tst_Aggregation();
    virtual ~tst_Aggregation();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

public slots:
#ifdef USING_QTPIM
    void addAccumulationSlot(const QList<QContactIdType> &ids);
    void chgAccumulationSlot(const QList<QContactIdType> &ids);
    void remAccumulationSlot(const QList<QContactIdType> &ids);
#else
    void addAccumulationSlot(const QList<QContactLocalId> &ids);
    void chgAccumulationSlot(const QList<QContactLocalId> &ids);
    void remAccumulationSlot(const QList<QContactLocalId> &ids);
#endif

private slots:
    void createSingleLocal();
    void createMultipleLocal();
    void createSingleLocalAndSingleSync();

    void updateSingleLocal();
    void updateSingleAggregate();
    void updateAggregateOfLocalAndSync();
    void updateAggregateOfLocalAndModifiableSync();

    void promotionToSingleLocal();
    void uniquenessConstraints();

    void removeSingleLocal();
    void removeSingleAggregate();

    void alterRelationships();

    void wasLocal();

    void aggregationHeuristic_data();
    void aggregationHeuristic();

    void regenerateAggregate();

    void detailUris();

    void correctDetails();

    void batchSemantics();

    void customSemantics();

    void changeLogFiltering();

private:
    void waitForSignalPropagation();

    QContactManager *m_cm;
    QSet<QContactIdType> m_addAccumulatedIds;
    QSet<QContactIdType> m_chgAccumulatedIds;
    QSet<QContactIdType> m_remAccumulatedIds;
    QSet<QContactIdType> m_createdIds;
};

tst_Aggregation::tst_Aggregation()
    : m_cm(new QContactManager(QLatin1String("org.nemomobile.contacts.sqlite")))
{
    QTest::qWait(250); // creating self contact etc will cause some signals to be emitted.  ignore them.
    connect(m_cm, contactsAddedSignal, this, addedAccumulationSlot);
    connect(m_cm, contactsChangedSignal, this, changedAccumulationSlot);
    connect(m_cm, contactsRemovedSignal, this, removedAccumulationSlot);
}

tst_Aggregation::~tst_Aggregation()
{
}

void tst_Aggregation::initTestCase()
{
    registerIdType();
}

void tst_Aggregation::init()
{
    m_addAccumulatedIds.clear();
    m_chgAccumulatedIds.clear();
    m_remAccumulatedIds.clear();
    m_createdIds.clear();
}

void tst_Aggregation::cleanupTestCase()
{
}

void tst_Aggregation::cleanup()
{
    waitForSignalPropagation();
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
    waitForSignalPropagation();
}

void tst_Aggregation::waitForSignalPropagation()
{
    // Signals are routed via DBUS, so we need to wait for them to arrive
    QTest::qWait(50);
}

void tst_Aggregation::addAccumulationSlot(const QList<QContactIdType> &ids)
{
    foreach (const QContactIdType &id, ids) {
        m_addAccumulatedIds.insert(id);
        m_createdIds.insert(id);
    }
}

void tst_Aggregation::chgAccumulationSlot(const QList<QContactIdType> &ids)
{
    foreach (const QContactIdType &id, ids) {
        m_chgAccumulatedIds.insert(id);
    }
}

void tst_Aggregation::remAccumulationSlot(const QList<QContactIdType> &ids)
{
    foreach (const QContactIdType &id, ids) {
        m_remAccumulatedIds.insert(id);
    }
}

void tst_Aggregation::createSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
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

    QContactGender ag;
    ag.setGender(QContactGender::GenderFemale);
    alice.saveDetail(&ag);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // Test the provenance of details
    QContactPhoneNumber localDetail(localAlice.detail<QContactPhoneNumber>());
    QContactPhoneNumber aggregateDetail(aggregateAlice.detail<QContactPhoneNumber>());
    QVERIFY(!detailProvenance(localDetail).isEmpty());
    QCOMPARE(detailProvenance(aggregateDetail), detailProvenance(localDetail));

    // A local contact should have a GUID, which is not promoted to the aggregate
    QVERIFY(!localAlice.detail<QContactGuid>().guid().isEmpty());
    QVERIFY(aggregateAlice.detail<QContactGuid>().guid().isEmpty());

    // Verify that gender is promoted
    QCOMPARE(localAlice.detail<QContactGender>().gender(), QContactGender::GenderFemale);
    QCOMPARE(aggregateAlice.detail<QContactGender>().gender(), QContactGender::GenderFemale);
}

void tst_Aggregation::createMultipleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
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

    // add an explicit GUID to Bob
    const QString bobGuid("I am Bob");
    QContactGuid bg;
    bg.setGuid(bobGuid);
    bob.saveDetail(&bg);

    QList<QContact> saveList;
    saveList << alice << bob;
    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContacts(&saveList));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate for each
    alice = saveList.at(0); bob = saveList.at(1);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 4);
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(bob)));
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
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(!relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(!relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QVERIFY(!relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(!relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // Test the provenance of details
    QContactPhoneNumber localAliceDetail(localAlice.detail<QContactPhoneNumber>());
    QContactPhoneNumber aggregateAliceDetail(aggregateAlice.detail<QContactPhoneNumber>());
    QVERIFY(!detailProvenance(localAliceDetail).isEmpty());
    QCOMPARE(detailProvenance(aggregateAliceDetail), detailProvenance(localAliceDetail));

    QContactPhoneNumber localBobDetail(localBob.detail<QContactPhoneNumber>());
    QContactPhoneNumber aggregateBobDetail(aggregateBob.detail<QContactPhoneNumber>());
    QVERIFY(!detailProvenance(localBobDetail).isEmpty());
    QCOMPARE(detailProvenance(aggregateBobDetail), detailProvenance(localBobDetail));
    QVERIFY(detailProvenance(localBobDetail) != detailProvenance(localAliceDetail));

    // Verify that the local consituents have GUIDs, but the aggregates don't
    QVERIFY(!localAlice.detail<QContactGuid>().guid().isEmpty());
    QVERIFY(!localBob.detail<QContactGuid>().guid().isEmpty());
    QCOMPARE(localBob.detail<QContactGuid>().guid(), bobGuid);
    QVERIFY(aggregateAlice.detail<QContactGuid>().guid().isEmpty());
    QVERIFY(aggregateBob.detail<QContactGuid>().guid().isEmpty());
}

void tst_Aggregation::createSingleLocalAndSingleSync()
{
    // here we create a local contact, and then save it
    // and then we create a "sync" contact, which should "match" it.
    // It should be related to the aggregate created for the sync.

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
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
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2);
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QTRY_COMPARE(m_addAccumulatedIds.size(), 3);
    QTRY_COMPARE(m_chgAccumulatedIds.size(), 1); // the aggregate should have been updated (with the hobby)
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
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
                localAlice = curr;
                foundLocalAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("test")) {
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(testAlice.id()));

    // Verify the propagation of details
    QContactHobby localDetail(localAlice.detail<QContactHobby>());
    QContactHobby testDetail(testAlice.detail<QContactHobby>());
    QContactHobby aggregateDetail(aggregateAlice.detail<QContactHobby>());

    QCOMPARE(testDetail.value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
    QVERIFY(!detailProvenance(testDetail).isEmpty());
    QCOMPARE(aggregateDetail.value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
    QCOMPARE(detailProvenance(aggregateDetail), detailProvenance(testDetail));
    QCOMPARE(localDetail.value<QString>(QContactHobby::FieldHobby), QString()); // local shouldn't get it
    QVERIFY(detailProvenance(localDetail).isEmpty());
}

void tst_Aggregation::updateSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
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

    QContactHobby ah;
    ah.setHobby("tennis");
    alice.saveDetail(&ah);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
        QContactHobby currHobby = curr.detail<QContactHobby>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("4567")
                && currHobby.hobby() == QLatin1String("tennis")) {
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
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // now update alice.  The aggregate should get updated also.
    QContactEmailAddress ae; // add an email address.
    ae.setEmailAddress("alice4@test.com");
    QVERIFY(localAlice.saveDetail(&ae));
    QContactHobby rah = localAlice.detail<QContactHobby>(); // remove a hobby
    QVERIFY(localAlice.removeDetail(&rah));
    QContactPhoneNumber maph = localAlice.detail<QContactPhoneNumber>(); // modify a phone number
    maph.setNumber("4444");
    QVERIFY(localAlice.saveDetail(&maph));
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&localAlice));
    QTRY_VERIFY(chgSpy.count() > chgSpyCount);
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // reload them, and compare.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(localAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test.com"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactEmailAddress>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test.com"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactEmailAddress>()), detailProvenance(localAlice.detail<QContactEmailAddress>()));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QVERIFY(localAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());
    QVERIFY(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());

    // now do an update with a definition mask.  We need to be certain that no masked details were lost.
    ae = localAlice.detail<QContactEmailAddress>();
    ae.setEmailAddress("alice4@test4.com");
    QVERIFY(localAlice.saveDetail(&ae));
    aph = localAlice.detail<QContactPhoneNumber>();
    QVERIFY(localAlice.removeDetail(&aph)); // removed, but since we don't include phone number in the definitionMask, shouldn't be applied
    QList<QContact> saveList;
    saveList << localAlice;
    QVERIFY(m_cm->saveContacts(&saveList, DetailList() << detailType<QContactEmailAddress>()));

    // reload them, and compare.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test4.com"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test4.com"));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
}

void tst_Aggregation::updateSingleAggregate()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
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

    QContactHobby ah;
    ah.setHobby("tennis");
    alice.saveDetail(&ah);

    QContactNickname ak;
    ak.setNickname("Ally");
    alice.saveDetail(&ak);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
        QContactHobby currHobby = curr.detail<QContactHobby>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("567")
                && currHobby.hobby() == QLatin1String("tennis")) {
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
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // now update aggregate alice.  We expect the changes to "down promoted" to the local contact!
    QContactEmailAddress ae; // add an email address
    ae.setEmailAddress("alice5@test.com");
    aggregateAlice.saveDetail(&ae);
    QContactHobby rah = aggregateAlice.detail<QContactHobby>(); // remove a hobby
    aggregateAlice.removeDetail(&rah);
    QContactPhoneNumber maph = aggregateAlice.detail<QContactPhoneNumber>(); // modify a phone number
    maph.setNumber("555");
    aggregateAlice.saveDetail(&maph);
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    QTRY_VERIFY(chgSpy.count() > chgSpyCount);
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // reload them, and compare.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(localAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(localAlice.details<QContactNickname>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice5@test.com"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactEmailAddress>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice5@test.com"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactEmailAddress>()), detailProvenance(localAlice.detail<QContactEmailAddress>()));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QLatin1String("555"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QLatin1String("555"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QVERIFY(localAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());
    QVERIFY(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());
    QCOMPARE(localAlice.detail<QContactNickname>().value<QString>(QContactNickname::FieldNickname), QLatin1String("Ally"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactNickname>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactNickname>().value<QString>(QContactNickname::FieldNickname), QLatin1String("Ally"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactNickname>()), detailProvenance(localAlice.detail<QContactNickname>()));
    QCOMPARE(detailProvenanceContact(localAlice.detail<QContactNickname>()), detailProvenanceContact(localAlice.detail<QContactEmailAddress>()));
}

void tst_Aggregation::updateAggregateOfLocalAndSync()
{
    // local alice
    QContact alice;
    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("PromotedLand");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("11111");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("aliceP@test.com");
    alice.saveDetail(&aem);

    QContactNickname ak;
    ak.setNickname("Ally");
    alice.saveDetail(&ak);

    QVERIFY(m_cm->saveContact(&alice));

    // sync alice
    QContact syncAlice;
    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    syncAlice.saveDetail(&san);

    QContactEmailAddress saem;
    saem.setEmailAddress(aem.emailAddress());
    syncAlice.saveDetail(&saem);

    QContactHobby sah; // this is a "new" detail which doesn't appear in the local contact.
    sah.setHobby(QLatin1String("tennis"));
    syncAlice.saveDetail(&sah);

    QContactNote sanote; // this is a "new" detail which doesn't appear in the local contact.
    sanote.setNote(QLatin1String("noteworthy note"));
    syncAlice.saveDetail(&sanote);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    QVERIFY(m_cm->saveContact(&syncAlice));

    // now grab the aggregate alice
    QContactRelationshipFilter aggf;
    setFilterContact(aggf, alice);
    aggf.setRelatedContactRole(QContactRelationship::Second);
    setFilterType(aggf, QContactRelationship::Aggregates);
    QList<QContact> allAggregatesOfAlice = m_cm->contacts(aggf);
    QCOMPARE(allAggregatesOfAlice.size(), 1);
    QContact aggregateAlice = allAggregatesOfAlice.at(0);

    // now ensure that updates / modifies / removals work as expected
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1); // comes from the local contact
    QContactPhoneNumber maph = aggregateAlice.detail<QContactPhoneNumber>();
    maph.setNumber("11115");
    QVERIFY(aggregateAlice.saveDetail(&maph)); // this should work, and modify the local contact's version.
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1); // there are two, but since the values were identical, should only have one!
    QContactEmailAddress mem = aggregateAlice.detail<QContactEmailAddress>();
    mem.setEmailAddress("aliceP2@test.com");
    QVERIFY(aggregateAlice.saveDetail(&mem)); // this has strange semantics.  It should modify the local contact's version
                                     // but the regenerated aggregate will have BOTH email addresses - since we cannot remove
                                     // or modify the email address from the sync-source contact.
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1); // comes from the sync contact

    QContactHobby rah = aggregateAlice.detail<QContactHobby>();
    QVERIFY(rah.accessConstraints() & QContactDetail::Irremovable);
    QVERIFY(rah.accessConstraints() & QContactDetail::ReadOnly);
    QVERIFY(!aggregateAlice.removeDetail(&rah)); // this should be irremovable, due to constraint on synced details

    /*BUG IN MOBILITY - contact.saveDetail() doesn't check read only constraints :-/
    QContactNote man = aggregateAlice.detail<QContactNote>();
    QVERIFY(man.accessConstraints() & QContactDetail::Irremovable);
    QVERIFY(man.accessConstraints() & QContactDetail::ReadOnly);
    man.setNote("modified note");
    QVERIFY(!aggregateAlice.saveDetail(&man)); // this should be read only, due to constraint on synced details
    */

    QVERIFY(m_cm->saveContact(&aggregateAlice));

    // regenerate and ensure we get what we expect.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);     // original, comes from the local contact
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);  // modified, comes from the local contact
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("11115"));
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);        // couldn't remove, comes from the sync contact
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactHobby>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString::fromLatin1("tennis"));
    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);         // couldn't modify, comes from the sync contact
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactNote>()) != detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    QCOMPARE(aggregateAlice.detail<QContactNote>().value<QString>(QContactNote::FieldNote), QString::fromLatin1("noteworthy note"));

    QList<QContactEmailAddress> aaems = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaems.size(), 2); // modified local, now have two different values
    if (aaems.at(0).emailAddress() == QLatin1String("aliceP@test.com")) { // order is undefined.
        QCOMPARE(detailProvenanceContact(aaems.at(0)), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
        QCOMPARE(aaems.at(1).emailAddress(), QLatin1String("aliceP2@test.com"));
        QCOMPARE(detailProvenanceContact(aaems.at(1)), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    } else {
        QCOMPARE(aaems.at(0).emailAddress(), QLatin1String("aliceP2@test.com"));
        QCOMPARE(detailProvenanceContact(aaems.at(0)), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
        QCOMPARE(aaems.at(1).emailAddress(), QLatin1String("aliceP@test.com"));
        QCOMPARE(detailProvenanceContact(aaems.at(1)), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
    }
}

void tst_Aggregation::updateAggregateOfLocalAndModifiableSync()
{
    // local alice
    QContact alice;
    {
        QContactName name;
        name.setFirstName("Alice");
        name.setMiddleName("In");
        name.setLastName("PromotedLand");
        alice.saveDetail(&name);

        QContactNickname nickname;
        nickname.setNickname("Ally");
        alice.saveDetail(&nickname);

        QContactPhoneNumber aph;
        aph.setNumber("11111");
        alice.saveDetail(&aph);
    }
    QVERIFY(m_cm->saveContact(&alice));

    const QContactName &localName(alice.detail<QContactName>());

    // first syncTarget alice
    QContact testAlice;
    {
        QContactName name;
        name.setFirstName(localName.firstName());
        name.setMiddleName(localName.middleName());
        name.setLastName(localName.lastName());
        testAlice.saveDetail(&name);

        QContactRingtone ringtone;
        ringtone.setAudioRingtoneUrl(QUrl("http://example.org/crickets.mp3"));
        testAlice.saveDetail(&ringtone);

        QContactEmailAddress emailAddress;
        emailAddress.setEmailAddress("aliceP@test.com");
        emailAddress.setValue(QContactDetail__FieldModifiable, true);
        testAlice.saveDetail(&emailAddress);

        QContactNote note;
        note.setNote("noteworthy note");
        note.setValue(QContactDetail__FieldModifiable, true);
        testAlice.saveDetail(&note);

        QContactHobby hobby;
        hobby.setHobby("tennis");
        hobby.setValue(QContactDetail__FieldModifiable, false);
        testAlice.saveDetail(&hobby);

        QContactSyncTarget syncTarget;
        syncTarget.setSyncTarget("test");
        testAlice.saveDetail(&syncTarget);

        QVERIFY(m_cm->saveContact(&testAlice));
    }

    // second syncTarget alice
    QContact trialAlice;
    {
        QContactName name;
        name.setFirstName(localName.firstName());
        name.setMiddleName(localName.middleName());
        name.setLastName(localName.lastName());
        trialAlice.saveDetail(&name);

        QContactTag tag;
        tag.setTag("Fiction");
        trialAlice.saveDetail(&tag);

        QContactEmailAddress emailAddress;
        emailAddress.setEmailAddress("alice@example.org");
        emailAddress.setValue(QContactDetail__FieldModifiable, true);
        trialAlice.saveDetail(&emailAddress);

        QContactOrganization organization;
        organization.setRole("CEO");
        organization.setValue(QContactDetail__FieldModifiable, true);
        trialAlice.saveDetail(&organization);

        QContactSyncTarget syncTarget;
        syncTarget.setSyncTarget("trial");
        trialAlice.saveDetail(&syncTarget);

        QVERIFY(m_cm->saveContact(&trialAlice));
    }

    // now grab the aggregate alice
    QContact aggregateAlice;
    {
        QContactRelationshipFilter filter;
        setFilterContact(filter, alice);
        filter.setRelatedContactRole(QContactRelationship::Second);
        setFilterType(filter, QContactRelationship::Aggregates);
        QList<QContact> allAggregatesOfAlice = m_cm->contacts(filter);
        QCOMPARE(allAggregatesOfAlice.size(), 1);
        aggregateAlice = allAggregatesOfAlice.at(0);
    }

    // Verify the aggregate state
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());

    // Nickname found only in the local contact
    const QString localContact(detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));

    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), localContact);

    QCOMPARE(aggregateAlice.details<QContactRingtone>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactRingtone>()).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactRingtone>()) != localContact);

    // Ringtone found only in the 'test' contact
    const QString testContact(detailProvenanceContact(aggregateAlice.detail<QContactRingtone>()));

    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 2);
    QVERIFY(!detailProvenance(aggregateAlice.details<QContactEmailAddress>().at(0)).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(0)) != localContact);
    QVERIFY(!detailProvenance(aggregateAlice.details<QContactEmailAddress>().at(1)).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(1)) != localContact);
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(0)) != detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(1)));

    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), testContact);

    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactHobby>()), testContact);

    QCOMPARE(aggregateAlice.details<QContactTag>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactTag>()).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactTag>()) != localContact);
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactTag>()) != testContact);

    // Tag found only in the 'trial' contact
    const QString trialContact(detailProvenanceContact(aggregateAlice.detail<QContactTag>()));

    QCOMPARE(aggregateAlice.details<QContactOrganization>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactOrganization>()), trialContact);

    // Test the modifiability of the details

    // Aggregate details are not modifiable
    QCOMPARE(aggregateAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(0).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(1).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactNote>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactOrganization>().value(QContactDetail__FieldModifiable).toBool(), false);

    // The test contact should have some modifiable fields
    testAlice = m_cm->contact(retrievalId(testAlice));
    QCOMPARE(testAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactRingtone>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
    QCOMPARE(testAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactNote>().value(QContactDetail__FieldModifiable).toBool(), true);

    // The trial contact should also have some modifiable fields
    trialAlice = m_cm->contact(retrievalId(trialAlice));
    QCOMPARE(trialAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactTag>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
    QCOMPARE(trialAlice.detail<QContactOrganization>().value(QContactDetail__FieldModifiable).toBool(), true);

    // now ensure that updates / modifies / removals work as expected
    {
        // locally-originated detail - should be modified
        QContactPhoneNumber phoneNumber = aggregateAlice.detail<QContactPhoneNumber>();
        phoneNumber.setNumber("22222");
        QVERIFY(aggregateAlice.saveDetail(&phoneNumber));

        // modifiable sync details - should be modified in each contact
        foreach (QContactEmailAddress emailAddress, aggregateAlice.details<QContactEmailAddress>()) {
            if (emailAddress.emailAddress() == QString::fromLatin1("aliceP@test.com")) {
                emailAddress.setEmailAddress("aliceP2@test.com");
                QVERIFY(aggregateAlice.saveDetail(&emailAddress));
            } else {
                emailAddress.setEmailAddress("alice2@example.org");
                QVERIFY(aggregateAlice.saveDetail(&emailAddress));
            }
        }

        // modifiable sync detail - should be removed
        QContactNote note = aggregateAlice.detail<QContactNote>();
        QVERIFY(aggregateAlice.removeDetail(&note));

        // modifiable sync detail - should be removed
        QContactOrganization organization = aggregateAlice.detail<QContactOrganization>();
        QVERIFY(aggregateAlice.removeDetail(&organization));

        // unmodifiable sync detail - modification should be pushed to local
        QContactHobby hobby = aggregateAlice.detail<QContactHobby>();
        hobby.setHobby("crochet");
        QVERIFY(aggregateAlice.saveDetail(&hobby));
    }

    QVERIFY(m_cm->saveContact(&aggregateAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());

    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), localContact);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().at(0).number(), QString::fromLatin1("22222"));

    QList<QContactEmailAddress> aaeas = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaeas.size(), 2);
    if (aaeas.at(0).emailAddress() == QString::fromLatin1("aliceP2@test.com")) {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), testContact);
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), trialContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("alice2@example.org"));
    } else {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), trialContact);
        QCOMPARE(aaeas.at(0).emailAddress(), QString::fromLatin1("alice2@example.org"));
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), testContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("aliceP2@test.com"));
    }

    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 0);

    QList<QContactHobby> aahs = aggregateAlice.details<QContactHobby>();
    QCOMPARE(aahs.size(), 2);
    if (aahs.at(0).hobby() == QString::fromLatin1("tennis")) {
        QCOMPARE(detailProvenanceContact(aahs.at(0)), testContact);
        QCOMPARE(aahs.at(1).hobby(), QString::fromLatin1("crochet"));
        QCOMPARE(detailProvenanceContact(aahs.at(1)), localContact);
    } else {
        QCOMPARE(aahs.at(0).hobby(), QString::fromLatin1("crochet"));
        QCOMPARE(detailProvenanceContact(aahs.at(0)), localContact);
        QCOMPARE(aahs.at(1).hobby(), QString::fromLatin1("tennis"));
        QCOMPARE(detailProvenanceContact(aahs.at(1)), testContact);
    }

    QCOMPARE(aggregateAlice.details<QContactOrganization>().size(), 0);

    // Modifiability should be unaffected

    // Aggregate details are not modifiable
    QCOMPARE(aggregateAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(0).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(1).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);

    // The test contact should have some modifiable fields
    testAlice = m_cm->contact(retrievalId(testAlice));
    QCOMPARE(testAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactRingtone>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
    QCOMPARE(testAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);

    // The trial contact should also have some modifiable fields
    trialAlice = m_cm->contact(retrievalId(trialAlice));
    QCOMPARE(trialAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactTag>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
}

void tst_Aggregation::promotionToSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    // first, save a sync target alice.  This should generate an aggregate, but not a local.
    QContact syncAlice;
    QContactName san;
    san.setFirstName(QLatin1String("Single"));
    san.setMiddleName(QLatin1String("Promotion"));
    san.setLastName(QLatin1String("ToAggregate"));
    syncAlice.saveDetail(&san);

    QContactEmailAddress saem;
    saem.setEmailAddress(QLatin1String("spta@test.com"));
    syncAlice.saveDetail(&saem);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    QVERIFY(m_cm->saveContact(&syncAlice));

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact aggregateAlice;
    QContact localAlice;
    bool foundLocalAlice = false;
    bool foundSyncAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName() == QLatin1String("ToAggregate")
                && currEm.emailAddress() == QLatin1String("spta@test.com")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(!foundLocalAlice); // shouldn't have created a local
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice); // should have created an aggregate
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    // now we favoritify the aggregate contact.
    // this should cause the creation of a local contact.
    // The favoriteness should be promoted down to the local, but not to the sync.
    // Also, the email address should not be promoted down to the local, as it
    // came from the sync.
    QContactFavorite afav = aggregateAlice.detail<QContactFavorite>();
    afav.setFavorite(true);
    QVERIFY(aggregateAlice.saveDetail(&afav)); // this overwrites the existing one.
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // should succeed, and update.

    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundSyncAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        QContactFavorite currFav = curr.detail<QContactFavorite>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName() == QLatin1String("ToAggregate")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                QVERIFY(!foundSyncAlice); // found more than one = error...
                QCOMPARE(currEm.emailAddress(), QLatin1String("spta@test.com"));
                QVERIFY(!currFav.isFavorite());
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                QVERIFY(!foundLocalAlice); // found more than one = error...
                QCOMPARE(currEm.emailAddress(), QString());
                QVERIFY(currFav.isFavorite());
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QVERIFY(!foundAggregateAlice); // found more than one = error...
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(currEm.emailAddress(), QLatin1String("spta@test.com"));
                QVERIFY(currFav.isFavorite());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we found them all
    QVERIFY(foundLocalAlice);
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice);

    // ensure they're related as required.
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    // finally, save a phone number in the aggregate.
    // this should get downpromoted to the local.
    // no more local contacts should be generated.
    QContactPhoneNumber aphn;
    aphn.setNumber("11111");
    QVERIFY(aggregateAlice.saveDetail(&aphn));
    QVERIFY(m_cm->saveContact(&aggregateAlice));

    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundSyncAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName() == QLatin1String("ToAggregate")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                QVERIFY(!foundSyncAlice); // found more than one = error...
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                QVERIFY(!foundLocalAlice); // found more than one = error...
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QVERIFY(!foundAggregateAlice); // found more than one = error...
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we found them all
    QVERIFY(foundLocalAlice);
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice);

    // ensure they're related as required.
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    QCOMPARE(localAlice.detail<QContactEmailAddress>().emailAddress(), QString());
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QVERIFY(localAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(syncAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QVERIFY(!detailProvenance(syncAlice.detail<QContactEmailAddress>()).isEmpty());
    QVERIFY(detailProvenanceContact(syncAlice.detail<QContactEmailAddress>()) != detailProvenanceContact(localAlice.detail<QContactPhoneNumber>()));
    QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), QString());
    QVERIFY(!syncAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactEmailAddress>()), detailProvenance(syncAlice.detail<QContactEmailAddress>()));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()) != detailProvenanceContact(aggregateAlice.detail<QContactEmailAddress>()));
    QVERIFY(aggregateAlice.detail<QContactFavorite>().isFavorite());

    // now unfavorite aggregate alice.  ensure that it propagates properly.
    afav = aggregateAlice.detail<QContactFavorite>();
    afav.setFavorite(false);
    QVERIFY(aggregateAlice.saveDetail(&afav));
    QVERIFY(m_cm->saveContact(&aggregateAlice));

    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundSyncAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName() == QLatin1String("ToAggregate")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                QVERIFY(!foundSyncAlice); // found more than one = error...
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                QVERIFY(!foundLocalAlice); // found more than one = error...
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QVERIFY(!foundAggregateAlice); // found more than one = error...
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we found them all
    QVERIFY(foundLocalAlice);
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice);

    QCOMPARE(localAlice.detail<QContactEmailAddress>().emailAddress(), QString());
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QVERIFY(!localAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(syncAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), QString());
    QVERIFY(!syncAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QVERIFY(!aggregateAlice.detail<QContactFavorite>().isFavorite());
}

void tst_Aggregation::uniquenessConstraints()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    // create a valid local contact.  An aggregate should be generated.
    QContact localAlice;
    QContactName an;
    an.setFirstName("Uniqueness");
    an.setLastName("Constraints");
    QVERIFY(localAlice.saveDetail(&an));
    QContactEmailAddress aem;
    aem.setEmailAddress("uniqueness@test.com");
    QVERIFY(localAlice.saveDetail(&aem));
    QVERIFY(m_cm->saveContact(&localAlice));

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Uniqueness")
                && currName.lastName() == QLatin1String("Constraints")
                && currEm.emailAddress() == QLatin1String("uniqueness@test.com")) {
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

    // test uniqueness constraint of favorite detail.
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 1);
    QContactFavorite afav;
    afav.setFavorite(true);
    QVERIFY(aggregateAlice.saveDetail(&afav)); // this actually creates a second (in memory) favorite detail
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // should fail, as Favorite is unique
    QVERIFY(aggregateAlice.removeDetail(&afav));
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 1);
    afav = aggregateAlice.detail<QContactFavorite>();
    afav.setFavorite(true);
    QVERIFY(aggregateAlice.saveDetail(&afav));   // should update the existing.
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // should succeed.
    QVERIFY(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactFavorite>().isFavorite());
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of birthday detail.
    QDateTime aliceBirthday = QDateTime::fromString("25/12/1950 01:23:45", "dd/MM/yyyy hh:mm:ss");
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 0);
    QContactBirthday abd;
    abd.setDateTime(aliceBirthday);
    QVERIFY(aggregateAlice.saveDetail(&abd));
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 1);
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    // now save another, should fail.
    QContactBirthday anotherBd;
    anotherBd.setDateTime(QDateTime::currentDateTime());
    QVERIFY(aggregateAlice.saveDetail(&anotherBd));
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // should fail, uniqueness.
    QVERIFY(aggregateAlice.removeDetail(&anotherBd));
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // back to just one, should succeed.
    QVERIFY(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactBirthday>().dateTime() == aliceBirthday);
    // now save a different birthday in another contact aggregated into alice.
    QContact testsyncAlice;
    QContactSyncTarget tsst;
    tsst.setSyncTarget("test");
    testsyncAlice.saveDetail(&tsst);
    QContactBirthday tsabd;
    tsabd.setDateTime(aliceBirthday.addDays(-5));
    testsyncAlice.saveDetail(&tsabd);
    QContactName tsaname;
    tsaname.setFirstName(an.firstName());
    tsaname.setLastName(an.lastName());
    testsyncAlice.saveDetail(&tsaname);
    QContactEmailAddress tsaem;
    tsaem.setEmailAddress(aem.emailAddress());
    testsyncAlice.saveDetail(&tsaem);
    QVERIFY(m_cm->saveContact(&testsyncAlice)); // should get aggregated into aggregateAlice.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice)); // reload
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 1); // should still only have one birthday - local should take precedence.
    QVERIFY(aggregateAlice.detail<QContactBirthday>().dateTime() == aliceBirthday);
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of name detail.
    QVERIFY(aggregateAlice.details<QContactName>().size() == 1);
    QContactName anotherName;
    anotherName.setFirstName("Testing");
    QVERIFY(aggregateAlice.saveDetail(&anotherName));
    QCOMPARE(aggregateAlice.details<QContactName>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QVERIFY(aggregateAlice.removeDetail(&anotherName));
    QCOMPARE(aggregateAlice.details<QContactName>().size(), 1);
    anotherName = aggregateAlice.detail<QContactName>();
    anotherName.setMiddleName("Middle");
    QVERIFY(aggregateAlice.saveDetail(&anotherName));
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness (and read-only) constraint of sync target.
    QVERIFY(aggregateAlice.details<QContactSyncTarget>().size() == 1);
    QCOMPARE(aggregateAlice.detail<QContactSyncTarget>().value<QString>(QContactSyncTarget::FieldSyncTarget), QString::fromLatin1("aggregate"));
    QContactSyncTarget ast;
    ast.setSyncTarget("uniqueness");
    QVERIFY(aggregateAlice.saveDetail(&ast));
    QCOMPARE(aggregateAlice.details<QContactSyncTarget>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QVERIFY(aggregateAlice.removeDetail(&ast));
    QCOMPARE(aggregateAlice.details<QContactSyncTarget>().size(), 1);
    ast = aggregateAlice.detail<QContactSyncTarget>();
    ast.setSyncTarget("uniqueness");
    QVERIFY(aggregateAlice.saveDetail(&ast));
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // should also fail, as sync target is read only.
    ast = aggregateAlice.detail<QContactSyncTarget>();
    ast.setSyncTarget("aggregate"); // reset the state.
    QVERIFY(aggregateAlice.saveDetail(&ast));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of timestamp detail.
    // Timestamp is a bit special, since if no values exist, we don't synthesise it,
    // even though it exists in the main table.
    QDateTime testDt = QDateTime::currentDateTime();
    QTime testDtTime = testDt.time();
    testDt.setTime(testDtTime.addMSecs(testDtTime.msec()*-1)); // get rid of millis as sqlite doesn't support them.
    bool hasCreatedTs = false;
    if (aggregateAlice.details<QContactTimestamp>().size() == 0) {
        QContactTimestamp firstTs;
        firstTs.setCreated(testDt);
        QVERIFY(aggregateAlice.saveDetail(&firstTs));
        QVERIFY(m_cm->saveContact(&aggregateAlice));
        hasCreatedTs = true;
    }
    QVERIFY(aggregateAlice.details<QContactTimestamp>().size() == 1);
    QContactTimestamp ats;
    ats.setLastModified(testDt);
    QVERIFY(aggregateAlice.saveDetail(&ats));
    QCOMPARE(aggregateAlice.details<QContactTimestamp>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QVERIFY(aggregateAlice.removeDetail(&ats));
    QCOMPARE(aggregateAlice.details<QContactTimestamp>().size(), 1);
    ats = aggregateAlice.detail<QContactTimestamp>();
    ats.setLastModified(testDt);
    QVERIFY(aggregateAlice.saveDetail(&ats));
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    QCOMPARE(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactTimestamp>().lastModified(), testDt);
    if (hasCreatedTs) {
        QCOMPARE(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactTimestamp>().created(), testDt);
    }
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of guid detail.  Guid is a bit special, as it's not in the main table.
    QVERIFY(aggregateAlice.details<QContactGuid>().size() == 0);
    QContactGuid ag;
    ag.setGuid("first-unique-guid");
    QVERIFY(aggregateAlice.saveDetail(&ag));
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // this succeeds, because GUID is NOT stored in main table.
    QContactGuid ag2;
    ag2.setGuid("second-unique-guid");
    QVERIFY(aggregateAlice.saveDetail(&ag2));
    QCOMPARE(aggregateAlice.details<QContactGuid>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // this fails, because now aggregateAlice has two.
    QVERIFY(aggregateAlice.removeDetail(&ag2));
    QCOMPARE(aggregateAlice.details<QContactGuid>().size(), 1);
    ag2 = aggregateAlice.detail<QContactGuid>();
    ag2.setGuid("second-unique-guid");
    QVERIFY(aggregateAlice.saveDetail(&ag2));
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // this now updates the original guid.
    QCOMPARE(aggregateAlice.detail<QContactGuid>().guid(), QLatin1String("second-unique-guid"));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
}

void tst_Aggregation::removeSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
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
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QVERIFY(m_cm->removeContact(removalId(localAlice)));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // but bob should not have been removed.
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));
    QList<QContact> stillExisting = m_cm->contacts(allSyncTargets);
    bool foundBob = false;
    foreach (const QContact &c, stillExisting) {
        if (c.id() == bob.id()) {
            foundBob = true;
            break;
        }
    }
    QVERIFY(foundBob);

    // now remove bob.
    QVERIFY(m_cm->removeContact(removalId(bob)));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allSyncTargets).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::removeSingleAggregate()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
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
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QVERIFY(m_cm->removeContact(removalId(aggregateAlice)));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // but bob should not have been removed.
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));
    QList<QContact> stillExisting = m_cm->contacts(allSyncTargets);
    bool foundBob = false;
    foreach (const QContact &c, stillExisting) {
        if (c.id() == bob.id()) {
            foundBob = true;
            break;
        }
    }
    QVERIFY(foundBob);

    // now remove bob.
    QVERIFY(m_cm->removeContact(removalId(bob)));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allSyncTargets).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::alterRelationships()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add two new contacts (with different sync targets)
    QContact alice;

    QContactName an;
    an.setMiddleName("Alice");
    an.setFirstName("test");
    an.setLastName("alterRelationships");
    alice.saveDetail(&an);

    QContactSyncTarget aliceST;
    aliceST.setSyncTarget("test-one");
    alice.saveDetail(&aliceST);

    QContact bob;

    QContactName bn;
    bn.setMiddleName("Bob");
    bn.setLastName("alterRelationships");
    bob.saveDetail(&bn);

    QContactSyncTarget bobST;
    bobST.setSyncTarget("test-two");
    bob.saveDetail(&bobST);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QVERIFY(m_cm->saveContact(&bob));
    QTRY_VERIFY(addSpy.count() >= addSpyCount + 2);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 4); // should have added locals + aggregates
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(bob)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 2); // 2 extra aggregate contacts
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 4); // should have added 2 normal + 2 aggregates
    allCount = m_cm->contactIds(allSyncTargets).size();

    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-one")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-two")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));

    // Remove the aggregation relationship for Bob
    QContactRelationship relationship;
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateBob.id(), localBob.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_COMPARE(m_remAccumulatedIds.size(), 1);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateBob)));
    remSpyCount = remSpy.count();

    // A new aggregate should have been generated
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 5);
    addSpyCount = addSpy.count();

    // Verify the relationships
    QContactId oldAggregateBobId = aggregateBob.id();

    localAlice = QContact();
    localBob = QContact();
    aggregateAlice = QContact();
    aggregateBob = QContact();

    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-one")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-two")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QVERIFY(aggregateBob.id() != oldAggregateBobId);

    // Aggregate localBob into aggregateAlice
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localBob.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Remove the relationship between localBob and aggregateBob
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateBob.id(), localBob.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_COMPARE(m_remAccumulatedIds.size(), 2);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateBob)));
    remSpyCount = remSpy.count();

    // No new aggregate should have been generated
    waitForSignalPropagation();
    QCOMPARE(addSpy.count(), addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 5);

    // Verify the relationships
    localAlice = QContact();
    localBob = QContact();
    aggregateAlice = QContact();
    aggregateBob = QContact();

    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-one")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-two")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QCOMPARE(aggregateBob.id(), QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));

    // Change Bob to have the same first and last name details as Alice
    bn = localBob.detail<QContactName>();
    bn.setFirstName("test");
    localBob.saveDetail(&bn);
    QVERIFY(m_cm->saveContact(&localBob));

    // Test removing a relationship from a multi-child aggregate
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localAlice.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // No aggregate will be removed
    waitForSignalPropagation();
    QCOMPARE(remSpy.count(), remSpyCount);
    QCOMPARE(m_remAccumulatedIds.size(), 2);

    // No new aggregate should have been generated, since the aggregation process will find
    // the existing aggregate as the best candidate (due to same first/last name)

    // Note - this test is failing with qt4; the match-finding query is failing to find the
    // existing match, due to some error in binding values that I can't work out right now...
    QCOMPARE(addSpy.count(), addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 5);

    // Verify that the relationships are unchanged
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // Create an IsNot relationship to prevent re-aggregation
    relationship = makeRelationship(QString::fromLatin1("IsNot"), aggregateAlice.id(), localAlice.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Now remove the aggregation again
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localAlice.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // No aggregate will be removed
    waitForSignalPropagation();
    QCOMPARE(remSpy.count(), remSpyCount);
    QCOMPARE(m_remAccumulatedIds.size(), 2);

    // A new aggregate should have been generated, since the aggregation can't use the existing match
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 6);
    addSpyCount = addSpy.count();

    // Verify that the relationships are updated
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QVERIFY(!relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(!relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
}

void tst_Aggregation::wasLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add two new local contacts (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setLastName("wasLocal");
    alice.saveDetail(&an);

    QContactPhoneNumber ap;
    ap.setNumber("1234567");
#ifdef USING_QTPIM
    ap.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
#else
    ap.setSubTypes(QStringList() << QContactPhoneNumber::SubTypeMobile);
#endif
    alice.saveDetail(&ap);

    QVERIFY(m_cm->saveContact(&alice));

    QContact bob;

    QContactNickname bn;
    bn.setNickname("Cooper");
    bob.saveDetail(&bn);

    QContactPhoneNumber bp;
    bp.setNumber("2345678");
#ifdef USING_QTPIM
    bp.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
#else
    bp.setSubTypes(QStringList() << QContactPhoneNumber::SubTypeMobile);
#endif
    bob.saveDetail(&bp);

    QVERIFY(m_cm->saveContact(&bob));

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QVERIFY(m_cm->saveContact(&bob));
    QTRY_VERIFY(addSpy.count() >= addSpyCount + 2);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 4); // should have added locals + aggregates
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(bob)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 2); // 2 extra aggregate contacts
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 4); // should have added locals + aggregates
    allCount = m_cm->contactIds(allSyncTargets).size();

    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        QContactNickname currNick = curr.detail<QContactNickname>();
        if (currName.firstName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("wasLocal")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currNick.nickname() == QLatin1String("Cooper")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    // Verify the presence of contacts
    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());

    // Verify the relationships
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));

    // Verify the details
    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(aggregateBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));

    // Convert localBob to was_local
    QContactSyncTarget bst = localBob.detail<QContactSyncTarget>();
    bst.setSyncTarget("was_local");
    QVERIFY(localBob.saveDetail(&bst));

    QVERIFY(m_cm->saveContact(&localBob));

    // Convert localBob to be a constituent of aggregateAlice
    QContactRelationship relationship;
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localBob.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Remove the aggregation relationship for Bob
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateBob.id(), localBob.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_COMPARE(m_remAccumulatedIds.size(), 1);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateBob)));
    remSpyCount = remSpy.count();

    // No new aggregate should have been created
    QCOMPARE(addSpy.count(), addSpyCount);

    // Verify the relationships
    localAlice = QContact();
    localBob = QContact();
    aggregateAlice = QContact();
    aggregateBob = QContact();

    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        QContactNickname currNick = curr.detail<QContactNickname>();
        if (currName.firstName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("wasLocal")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currNick.nickname() == QLatin1String("Cooper")) {
            if (currSt.syncTarget() == QLatin1String("was_local")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() == QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);
    foreach (const QContactPhoneNumber &phoneNumber, aggregateAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("2345678"));
    }

    // Was-local details are aggregated
    QCOMPARE(aggregateAlice.detail<QContactNickname>().nickname(), QLatin1String("Cooper"));
    QCOMPARE(localAlice.detail<QContactNickname>().nickname(), QString());

    // Changes are not promoted down to the was_local constituent
    QContactPhoneNumber pn;
    pn.setNumber("7654321");
#ifdef USING_QTPIM
    pn.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
#else
    pn.setSubTypes(QStringList() << QContactPhoneNumber::SubTypeMobile);
#endif
    aggregateAlice.saveDetail(&pn);

    QVERIFY(m_cm->saveContact(&aggregateAlice));

    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    localBob = m_cm->contact(retrievalId(localBob));

    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 2);
    foreach (const QContactPhoneNumber &phoneNumber, localAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("7654321"));
    }
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 3);
    foreach (const QContactPhoneNumber &phoneNumber, aggregateAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("2345678") ||
                phoneNumber.number() == QLatin1String("7654321"));
    }
}

void tst_Aggregation::aggregationHeuristic_data()
{
    QTest::addColumn<bool>("shouldAggregate");
    QTest::addColumn<QString>("aFirstName");
    QTest::addColumn<QString>("aMiddleName");
    QTest::addColumn<QString>("aLastName");
    QTest::addColumn<QString>("aNickname");
    QTest::addColumn<QString>("aPhoneNumber");
    QTest::addColumn<QString>("aEmailAddress");
    QTest::addColumn<QString>("aOnlineAccount");
    QTest::addColumn<QString>("bFirstName");
    QTest::addColumn<QString>("bMiddleName");
    QTest::addColumn<QString>("bLastName");
    QTest::addColumn<QString>("bNickname");
    QTest::addColumn<QString>("bPhoneNumber");
    QTest::addColumn<QString>("bEmailAddress");
    QTest::addColumn<QString>("bOnlineAccount");

    // shared details / family members
    QTest::newRow("shared email") << false /* husband and wife, sharing email, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "" << "gumboots@test.com" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "" << "gumboots@test.com" << "";
    QTest::newRow("shared phone") << false /* husband and wife, sharing phone, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "111992888337" << "" << "";
    QTest::newRow("shared phone+email") << false /* husband and wife, sharing phone+email, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "111992888337" << "gumboots@test.com" << "";
    QTest::newRow("shared phone+email+account") << false /* husband and wife, sharing phone+email+account, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "gumboots@familysocial"
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "111992888337" << "gumboots@test.com" << "gumboots@familysocial";

    // different contactable details / same name
    QTest::newRow("match name, different p/e/a") << true /* identical name match is enough to match the contact */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";

    // fragment name, overlapping contactable
    QTest::newRow("initial fragment fname, identical p/e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("final fragment fname, identical p/e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Rick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("internal fragment fname, identical p/e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Deric" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("fragment mname, identical p/e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("fragment f/mname, identical p/e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("fragment fname, identical p, no e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << ""
        << "Fred" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << "";
    QTest::newRow("fragment mname, identical p, no e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << ""
        << "Frederick" << "" << "Gumboots" << "Freddy" << "111992888337" << "" << "";
    QTest::newRow("fragment f/mname, identical p, no e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << ""
        << "Fred" << "" << "Gumboots" << "Freddy" << "111992888337" << "" << "";
    QTest::newRow("fragment fname, identical p, different e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("fragment mname, identical p, different e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "" << "Gumboots" << "Freddy" << "111992888337" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("fragment f/mname, identical p, different e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "" << "Gumboots" << "Freddy" << "111992888337" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("fragment fname, identical e, different p/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "William Preston" << "Gumboots" << "Freddy" << "999118222773" << "gumboots@test.com" << "fgumboots@coolsocial";
    QTest::newRow("fragment mname, identical e, different p/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "" << "Gumboots" << "Freddy" << "999118222773" << "gumboots@test.com" << "fgumboots@coolsocial";
    QTest::newRow("fragment f/mname, identical e, different p/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "" << "Gumboots" << "Freddy" << "999118222773" << "gumboots@test.com" << "fgumboots@coolsocial";
    QTest::newRow("fragment fname, identical a, different p/e") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "William Preston" << "Gumboots" << "Freddy" << "999118222773" << "freddy@test.net" << "freddy00001@socialaccount";
    QTest::newRow("fragment mname, identical a, different p/e") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "" << "Gumboots" << "Freddy" << "999118222773" << "freddy@test.net" << "freddy00001@socialaccount";
    QTest::newRow("fragment f/mname, identical a, different p/e") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "" << "Gumboots" << "Freddy" << "999118222773" << "freddy@test.net" << "freddy00001@socialaccount";
    QTest::newRow("fragment f/mname, identical p, no e/a/nick") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << ""
        << "Fred" << "" << "Gumboots" << "" << "111992888337" << "" << "";
    QTest::newRow("fragment f/mname, identical p, no e/a/nick, name case") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << ""
        << "fred" << "" << "gumboots" << "" << "111992888337" << "" << "";

    // identical contacts should be aggregated
    QTest::newRow("identical, complete") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -fname") << true
        << "" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -mname") << true
        << "Frederick" << "" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -lname") << true
        << "Frederick" << "William Preston" << "" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -nick") << true
        << "Frederick" << "William Preston" << "Gumboots" << "" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -phone") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -email") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "" << "freddy00001@socialaccount";
    QTest::newRow("identical, -account") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << ""
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "";
    QTest::newRow("identical, diff nick") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Ricky" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";

    // f/l name differences should stop aggregation.  middle name doesn't count in the aggregation heuristic.
    QTest::newRow("fname different") << false
        << "Frederick" << "" << "Gumboots" << "" << "111992888337" << "" << ""
        << "Jillian" << "" << "Gumboots" << "" << "999118222773" << "" << "";
    QTest::newRow("lname different") << false
        << "Frederick" << "" << "Gumboots" << "" << "111992888337" << "" << ""
        << "Frederick" << "" << "Galoshes" << "" << "999118222773" << "" << "";

    // similarities in name, different contactable details
    QTest::newRow("similar name, different p/e/a") << false /* Only the last names match; not enough */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "" << "" << "Gumboots" << "" << "999118222773" << "anastacia@test.net" << "agumboots@coolsocial";
}

void tst_Aggregation::aggregationHeuristic()
{
    // this test exists to validate the findMatchingAggregate query.
    QFETCH(bool, shouldAggregate);
    QFETCH(QString, aFirstName);
    QFETCH(QString, aMiddleName);
    QFETCH(QString, aLastName);
    QFETCH(QString, aNickname);
    QFETCH(QString, aPhoneNumber);
    QFETCH(QString, aEmailAddress);
    QFETCH(QString, aOnlineAccount);
    QFETCH(QString, bFirstName);
    QFETCH(QString, bMiddleName);
    QFETCH(QString, bLastName);
    QFETCH(QString, bNickname);
    QFETCH(QString, bPhoneNumber);
    QFETCH(QString, bEmailAddress);
    QFETCH(QString, bOnlineAccount);

    for (int i = 0; i < 2; ++i) {
        QContact a, b;
        QContactSyncTarget async, bsync;
        QContactName aname, bname;
        QContactNickname anick, bnick;
        QContactPhoneNumber aphn, bphn;
        QContactEmailAddress aem, bem;
        QContactOnlineAccount aoa, boa;

        // construct a
        async.setSyncTarget("aggregation-heuristic-a");
        a.saveDetail(&async);

        aname.setFirstName(aFirstName);
        aname.setMiddleName(aMiddleName);
        aname.setLastName(aLastName);
        if (!aFirstName.isEmpty() || !aMiddleName.isEmpty() || !aLastName.isEmpty()) {
            a.saveDetail(&aname);
        }

        if (!aNickname.isEmpty()) {
            anick.setNickname(aNickname);
            a.saveDetail(&anick);
        }

        if (!aPhoneNumber.isEmpty()) {
            aphn.setNumber(aPhoneNumber);
            a.saveDetail(&aphn);
        }

        if (!aEmailAddress.isEmpty()) {
            aem.setEmailAddress(aEmailAddress);
            a.saveDetail(&aem);
        }

        if (!aOnlineAccount.isEmpty()) {
            aoa.setAccountUri(aOnlineAccount);
            a.saveDetail(&aoa);
        }

        // construct b
        bsync.setSyncTarget("aggregation-heuristic-b");
        b.saveDetail(&bsync);

        bname.setFirstName(bFirstName);
        bname.setMiddleName(bMiddleName);
        bname.setLastName(bLastName);
        if (!bFirstName.isEmpty() || !bMiddleName.isEmpty() || !bLastName.isEmpty()) {
            b.saveDetail(&bname);
        }

        if (!bNickname.isEmpty()) {
            bnick.setNickname(bNickname);
            b.saveDetail(&bnick);
        }

        if (!bPhoneNumber.isEmpty()) {
            bphn.setNumber(bPhoneNumber);
            b.saveDetail(&bphn);
        }

        if (!bEmailAddress.isEmpty()) {
            bem.setEmailAddress(bEmailAddress);
            b.saveDetail(&bem);
        }

        if (!bOnlineAccount.isEmpty()) {
            boa.setAccountUri(bOnlineAccount);
            b.saveDetail(&boa);
        }

        // Now perform the saves and see if we get some aggregation as required.
        int count = m_cm->contactIds().count();
        QVERIFY(m_cm->saveContact(i == 0 ? &a : &b));
        QCOMPARE(m_cm->contactIds().count(), (count+1));
        QVERIFY(m_cm->saveContact(i == 0 ? &b : &a));
        QCOMPARE(m_cm->contactIds().count(), shouldAggregate ? (count+1) : (count+2));

#ifdef USING_QTPIM
        m_cm->removeContact(a.id());
        m_cm->removeContact(b.id());
#else
        m_cm->removeContact(a.localId());
        m_cm->removeContact(b.localId());
#endif
    }
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
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
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
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2);
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
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
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QTRY_COMPARE(m_addAccumulatedIds.size(), 3);
    QTRY_COMPARE(m_chgAccumulatedIds.size(), 1); // the aggregate should have been updated (with the hobby)
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
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
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString()); // local shouldn't get it
                localAlice = curr;
                foundLocalAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("test")) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(testAlice.id()));

    // now remove the "test" sync contact
    QVERIFY(m_cm->removeContact(removalId(testAlice)));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(testAlice))); // should have been removed

    // but the other contacts should NOT have been removed
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(localAlice)));
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(aggregateAlice)));

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
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString());
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }
}

void tst_Aggregation::detailUris()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

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
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
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
}

void tst_Aggregation::batchSemantics()
{
    // for performance reasons, the engine assumes:
    // 1) sync targets of all contacts in a batch save must be the same
    // 2) no two contacts from the same sync target should be aggregated together

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    int allContactsCount = allContacts.size();

    QContact a, b, c;
    QContactName aname, bname, cname;
    QContactSyncTarget ast, bst, cst;
    aname.setFirstName("a");
    aname.setLastName("batch");
    bname.setFirstName("b");
    bname.setLastName("batch");
    cname.setFirstName("c");
    cname.setLastName("batch");
    ast.setSyncTarget("async");
    bst.setSyncTarget("bsync");
    cst.setSyncTarget("csync");

    a.saveDetail(&aname);
    a.saveDetail(&ast);
    b.saveDetail(&bname);
    b.saveDetail(&bst);
    c.saveDetail(&cname);
    c.saveDetail(&cst);

    // a) batch save should fail due to different sync targets.
    QList<QContact> saveList;
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // b) same as (a)
    cst.setSyncTarget("bsync");
    c.saveDetail(&cst);
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // c) same as (a) although in this case, local / empty are considered identical
    ast.setSyncTarget("local");
    bst.setSyncTarget(QString());
    cst.setSyncTarget("csync");
    a.saveDetail(&ast);
    b.saveDetail(&bst);
    c.saveDetail(&cst);
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // d) now it should succeed.
    cst.setSyncTarget("local");
    c.saveDetail(&cst);
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(m_cm->saveContacts(&saveList));

    allContacts = m_cm->contacts(allSyncTargets);
    int newContactsCount = allContacts.size() - allContactsCount;
    QCOMPARE(newContactsCount, 6); // 3 local, 3 aggregate

    // Now we test the semantic of "no two contacts from the same sync target should get aggregated"
    QContact d, e;
    QContactName dname, ename;
    QContactSyncTarget dst, est;
    dname.setFirstName("d");
    dname.setLastName("batch");
    ename.setFirstName("d");
    ename.setLastName("batch");
    ast.setSyncTarget("batch-sync");
    bst.setSyncTarget("batch-sync");

    d.saveDetail(&dname);
    d.saveDetail(&dst);
    e.saveDetail(&ename);
    e.saveDetail(&est);

    saveList.clear();
    saveList << d << e;
    QVERIFY(m_cm->saveContacts(&saveList));

    allContacts = m_cm->contacts(allSyncTargets);
    newContactsCount = allContacts.size() - allContactsCount;
    QCOMPARE(newContactsCount, 10); // 5 local, 5 aggregate - d and e should not have been aggregated into one.
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
    aa.setImageUrl(QUrl(QString::fromLatin1("test.png")));
    aa.setValue(QContactAvatar__FieldAvatarMetadata, "cover");
    alice.saveDetail(&aa);
    QVERIFY(m_cm->saveContact(&alice));
    QContact aliceReloaded = m_cm->contact(retrievalId(alice));
    QCOMPARE(aliceReloaded.detail<QContactName>().value<QString>(QContactName::FieldFirstName), QLatin1String("Alice"));
    QCOMPARE(QUrl(aliceReloaded.detail<QContactAvatar>().value<QString>(QContactAvatar::FieldImageUrl)).toString(), QUrl(QString::fromLatin1("test.png")).toString());
    QCOMPARE(aliceReloaded.detail<QContactAvatar>().value<QString>(QContactAvatar__FieldAvatarMetadata), QLatin1String("cover"));

    // 2 - test the self contact semantics
    QCOMPARE(m_cm->selfContactId(), ContactId::apiId(2));
    QVERIFY(!m_cm->setSelfContactId(ContactId::apiId(alice)));

    // cleanup.
    m_cm->removeContact(removalId(alice));
}

#define TRIM_DT_MSECS(dt) QDateTime::fromString(dt.toUTC().toString("yyyy-MM-dd hh:mm:ss"), "yyyy-MM-dd hh:mm:ss")
void tst_Aggregation::changeLogFiltering()
{
    // the qtcontacts-sqlite engine automatically adds creation and modification
    // timestamps if they're not already set.
    // NOTE: sqlite doesn't store milliseconds!

    QTest::qWait(2000); // wait two seconds, to ensure unique timestamps for saved contacts.
    QDateTime startTime = TRIM_DT_MSECS(QDateTime::currentDateTimeUtc());
    QDateTime minus5 = TRIM_DT_MSECS(startTime.addDays(-5));
    QDateTime minus3 = TRIM_DT_MSECS(startTime.addDays(-3));
    QDateTime minus2 = TRIM_DT_MSECS(startTime.addDays(-2));

    // 1) if provided, should not be overwritten
    QContact a;
    QContactName an;
    an.setFirstName("Alice");
    a.saveDetail(&an);
    QContactTimestamp at;
    at.setCreated(minus5);
    at.setLastModified(minus2);
    a.saveDetail(&at);

    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), minus5);
    QCOMPARE(at.lastModified(), minus2);

    // 2) if modified timestamp not provided, should be automatically generated.
    at.setLastModified(QDateTime());
    a.saveDetail(&at);
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), minus5);
    QVERIFY(at.lastModified() >= startTime);
    QVERIFY(at.lastModified() <= QDateTime::currentDateTimeUtc());

    // 3) created timestamp should only be generated on creation, not normal save.
    at.setCreated(QDateTime());
    a.saveDetail(&at);
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), QDateTime());
    QVERIFY(at.lastModified() >= startTime);
    QVERIFY(at.lastModified() <= QDateTime::currentDateTimeUtc());

    QContact b;
    QContactName bn;
    bn.setFirstName("Bob");
    b.saveDetail(&bn);
    QVERIFY(m_cm->saveContact(&b));
    b = m_cm->contact(retrievalId(b));
    QContactTimestamp bt = b.detail<QContactTimestamp>();
    QVERIFY(bt.created() >= startTime);
    QVERIFY(bt.created() <= QDateTime::currentDateTimeUtc());
    QVERIFY(bt.lastModified() >= startTime);
    QVERIFY(bt.lastModified() <= QDateTime::currentDateTimeUtc());

    // 4) ensure filtering works as expected.
    // First, ensure timestamps are filterable;
    // invalid date times are always included in filtered results.
    at.setCreated(minus5);
    at.setLastModified(minus2);
    a.saveDetail(&at);
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), minus5);
    QCOMPARE(at.lastModified(), minus2);

    QContactIntersectionFilter cif;
    QContactDetailFilter stf;
    setFilterDetail<QContactSyncTarget>(stf, QContactSyncTarget::FieldSyncTarget);
    stf.setValue("local"); // explicitly ignore aggregates.
    QContactChangeLogFilter clf;
    clf.setSince(startTime);

    clf.setEventType(QContactChangeLogFilter::EventAdded);
    cif.clear(); cif << stf << clf;
    QList<QContactIdType> filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.contains(retrievalId(b)));
    QVERIFY(!filtered.contains(retrievalId(a)));

    clf.setEventType(QContactChangeLogFilter::EventChanged);
    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.contains(retrievalId(b)));
    QVERIFY(!filtered.contains(retrievalId(a)));

    clf.setSince(minus3); // a was modified at minus2 so it should now be included.
    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.contains(retrievalId(b)));
    QVERIFY(filtered.contains(retrievalId(a)));
}

QTEST_MAIN(tst_Aggregation)
#include "tst_aggregation.moc"
