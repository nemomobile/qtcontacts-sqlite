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

#include <QContactManager>
#include <QContactFetchRequest>
#include <QContactFavorite>
#include <QContactName>
#include <QContactEmailAddress>
#include <QContactPhoneNumber>
#include <QContactDisplayLabel>
#include <QContactHobby>
#include <QContactAvatar>
#include <QContactAddress>
#include <QContactDetailFilter>
#include <QContactFetchHint>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QtDebug>

USE_CONTACTS_NAMESPACE

static QStringList generateFirstNamesList()
{
    QStringList retn;
    retn << "Alexandria" << "Andrew" << "Bob" << "Bronte" << "Clarence" << "Chandler"
         << "Dominic" << "Diedre" << "Eric" << "Esther" << "Felicity" << "Fred"
         << "Gary" << "Gertrude" << "Hillary" << "Henry" << "Jake" << "Jane"
         << "Larry" << "Lilliane" << "Mary" << "Mark" << "Nathene" << "Nicholas"
         << "Othello" << "Oscar" << "Penny" << "Peter";
    return retn;
}

static QStringList generateMiddleNamesList()
{
    QStringList retn;
    retn << "Aubrey" << "Cody" << "Taylor";
    return retn;
}

static QStringList generateLastNamesList()
{
    QStringList retn;
    retn << "Smith" << "Zimmerman" << "Baker" << "Porter" << "Tailor";
    return retn;
}

static QStringList generatePhoneNumbersList()
{
    QStringList retn;
    retn << "111222" << "111333" << "111444" << "111555" << "111666"
         << "111777" << "111888" << "111999" << "222333" << "222444"
         << "222555" << "222666" << "222777" << "222888" << "222999";
    return retn;
}

static QStringList generateEmailProvidersList()
{
    QStringList retn;
    retn << "@test.com" << "@testing.com" << "@testers.com"
         << "@test.org" << "@testing.org" << "@testers.org";
    return retn;
}

static QStringList generateAvatarsList()
{
    QStringList retn;
    retn << "-smiling.jpg" << "-laughing.jpg" << "-surprised.jpg"
         << "-smiling.png" << "-laughing.png" << "-surprised.png";
    return retn;
}

static QStringList generateHobbiesList()
{
    QStringList retn;
    retn << "tennis" << "soccer" << "squash" << "volleyball"
         << "chess" << "photography" << "painting" << "sketching";
    return retn;
}

QContact generateContact()
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

    // We always have a name, however.
    QContactName name;
    name.setFirstName(firstNames.at(random % firstNames.size()));
    if ((random % 6) == 0) name.setMiddleName(middleNames.at(random % middleNames.size()));
    name.setLastName(lastNames.at(random % lastNames.size()));
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
        phn.setNumber(phoneNumbers.at(random % phoneNumbers.size()));
        if ((random % 9) == 0) phn.setContexts(QContactDetail::ContextWork);
        retn.saveDetail(&phn);
    }

    // Email
    if ((random % 2) == 0) {
        QContactEmailAddress em;
        em.setEmailAddress(QString(QLatin1String("%1%2%3"))
                .arg(name.firstName()).arg(name.lastName())
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

int main(int argc, char  *argv[])
{
    QCoreApplication application(argc, argv);

    QContactManager manager(QLatin1String("org.nemomobile.contacts.sqlite"));

    QContactFetchRequest request;
    request.setManager(&manager);

    QElapsedTimer asyncTotalTimer;
    asyncTotalTimer.start();

    // Fetch all, no optimization hints
    for (int i = 0; i < 3; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << i << ": Fetch completed in" << elapsed << "ms";
    }

    // Skip relationships
    QContactFetchHint hint;
    hint.setOptimizationHints(QContactFetchHint::NoRelationships);
    request.setFetchHint(hint);

    for (int i = 0; i < 3; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << i << ": No-relationships fetch completed in" << elapsed << "ms";
    }

    // Reduce data access
#ifdef USING_QTPIM
    hint.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactName::Type << QContactAddress::Type);
#else
    hint.setDetailDefinitionsHint(QStringList() << QContactName::DefinitionName << QContactAddress::DefinitionName);
#endif
    request.setFetchHint(hint);

    for (int i = 0; i < 3; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << i << ": Reduced data fetch completed in" << elapsed << "ms";
    }

    // Reduce number of results
    hint.setMaxCountHint(request.contacts().count() / 8);
    request.setFetchHint(hint);

    for (int i = 0; i < 3; ++i) {
        QElapsedTimer timer;
        timer.start();
        request.start();
        request.waitForFinished();

        qint64 elapsed = timer.elapsed();
        qDebug() << i << ": Max count fetch completed in" << elapsed << "ms";
    }
    qint64 asyncTotalElapsed = asyncTotalTimer.elapsed();



    // Time some synchronous operations.  First, generate the test data.
    qsrand((int)asyncTotalElapsed);
    QList<int> nbrContacts;
    nbrContacts << 10 << 100 << 500 << 1000 << 2000 << 10000;
    QList<QList<QContact> > testData;
    qDebug() << "\n\n\n\n\n";
    qDebug() << "Generating test data for timings...";
    for (int i = 0; i < nbrContacts.size(); ++i) {
        int howMany = nbrContacts.at(i);
        QList<QContact> newTestData;
        newTestData.reserve(howMany);

        for (int j = 0; j < howMany; ++j) {
            newTestData.append(generateContact());
        }

        testData.append(newTestData);
    }

    // Perform the timings - these all create new contacts and assume an "empty" initial database
    QElapsedTimer syncTimer;
    for (int i = 0; i < testData.size(); ++i) {
        QList<QContact> td = testData.at(i);
        qint64 ste = 0;
        qDebug() << "Performing tests for" << td.size() << "contacts:";

        syncTimer.start();
        manager.saveContacts(&td);
        ste = syncTimer.elapsed();
        qDebug() << "    saving took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

        QContactFetchHint fh;
        syncTimer.start();
        QList<QContact> readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, all details, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type
                << QContactPhoneNumber::Type << QContactEmailAddress::Type);
#else
        fh.setDetailDefinitionsHint(QStringList() << QContactDisplayLabel::DefinitionName
                << QContactName::DefinitionName << QContactAvatar::DefinitionName
                << QContactPhoneNumber::DefinitionName << QContactEmailAddress::DefinitionName);
#endif
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, common details, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

        fh.setOptimizationHints(QContactFetchHint::NoRelationships);
#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
#else
        fh.setDetailDefinitionsHint(QStringList());
#endif
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, no relationships, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type);
#else
        fh.setDetailDefinitionsHint(QStringList() << QContactDisplayLabel::DefinitionName
                << QContactName::DefinitionName << QContactAvatar::DefinitionName);
#endif
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, display details + no rels, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

        QContactDetailFilter firstNameStartsA;
#ifdef USING_QTPIM
        firstNameStartsA.setDetailType(QContactName::Type, QContactName::FieldFirstName);
#else
        firstNameStartsA.setDetailDefinitionName(QContactName::DefinitionName, QContactName::FieldFirstName);
#endif
        firstNameStartsA.setValue("A");
        firstNameStartsA.setMatchFlags(QContactDetailFilter::MatchStartsWith);
#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
#else
        fh.setDetailDefinitionsHint(QStringList());
#endif
        syncTimer.start();
        readContacts = manager.contacts(firstNameStartsA, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading filtered, no relationships, took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

#ifdef USING_QTPIM
        QList<QContactId> idsToRemove;
        for (int j = 0; j < td.size(); ++j) {
            idsToRemove.append(td.at(j).id());
        }
#else
        QList<QContactLocalId> idsToRemove;
        for (int j = 0; j < td.size(); ++j) {
            idsToRemove.append(td.at(j).localId());
        }
#endif
        syncTimer.start();
        manager.removeContacts(idsToRemove);
        ste = syncTimer.elapsed();
        qDebug() << "    removing test data took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
    }

    // these tests are slightly different to those above.  They operate on much smaller
    // batches, but occur after the database has already been prefilled with some data.
    QList<int> smallerNbrContacts;
    smallerNbrContacts << 1 << 2 << 5 << 10 << 20 << 50;
    QList<QList<QContact> > smallerTestData;
    qDebug() << "\n\n";
    qDebug() << "Generating smaller test data for prefilled timings...";
    for (int i = 0; i < smallerNbrContacts.size(); ++i) {
        int howMany = smallerNbrContacts.at(i);
        QList<QContact> newTestData;
        newTestData.reserve(howMany);

        for (int j = 0; j < howMany; ++j) {
            newTestData.append(generateContact());
        }

        smallerTestData.append(newTestData);
    }

    // prefill the database
    QList<QContact> prefillData;
    for (int i = 0; i < testData.size() && testData.at(i).size() < 1001; ++i) {
        prefillData = testData.at(i);
    }
    qDebug() << "Prefilling database with" << prefillData.size() << "contacts... this will take a while...";
    manager.saveContacts(&prefillData);
    qDebug() << "Now performing timings...";
    for (int i = 0; i < smallerTestData.size(); ++i) {
        QList<QContact> td = smallerTestData.at(i);
        qint64 ste = 0;
        qDebug() << "Performing tests for" << td.size() << "contacts:";

        syncTimer.start();
        manager.saveContacts(&td);
        ste = syncTimer.elapsed();
        qDebug() << "    saving took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";

        QContactFetchHint fh;
        syncTimer.start();
        QList<QContact> readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, all details, took" << ste << "milliseconds";

#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type
                << QContactPhoneNumber::Type << QContactEmailAddress::Type);
#else
        fh.setDetailDefinitionsHint(QStringList() << QContactDisplayLabel::DefinitionName
                << QContactName::DefinitionName << QContactAvatar::DefinitionName
                << QContactPhoneNumber::DefinitionName << QContactEmailAddress::DefinitionName);
#endif
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, common details, took" << ste << "milliseconds";

        fh.setOptimizationHints(QContactFetchHint::NoRelationships);
#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
#else
        fh.setDetailDefinitionsHint(QStringList());
#endif
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, no relationships, took" << ste << "milliseconds";

#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>() << QContactDisplayLabel::Type
                << QContactName::Type << QContactAvatar::Type);
#else
        fh.setDetailDefinitionsHint(QStringList() << QContactDisplayLabel::DefinitionName
                << QContactName::DefinitionName << QContactAvatar::DefinitionName);
#endif
        syncTimer.start();
        readContacts = manager.contacts(QContactFilter(), QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading all, display details + no rels, took" << ste << "milliseconds";

        QContactDetailFilter firstNameStartsA;
#ifdef USING_QTPIM
        firstNameStartsA.setDetailType(QContactName::Type, QContactName::FieldFirstName);
#else
        firstNameStartsA.setDetailDefinitionName(QContactName::DefinitionName, QContactName::FieldFirstName);
#endif
        firstNameStartsA.setValue("A");
        firstNameStartsA.setMatchFlags(QContactDetailFilter::MatchStartsWith);
#ifdef USING_QTPIM
        fh.setDetailTypesHint(QList<QContactDetail::DetailType>());
#else
        fh.setDetailDefinitionsHint(QStringList());
#endif
        syncTimer.start();
        readContacts = manager.contacts(firstNameStartsA, QList<QContactSortOrder>(), fh);
        ste = syncTimer.elapsed();
        qDebug() << "    reading filtered, no relationships, took" << ste << "milliseconds";

#ifdef USING_QTPIM
        QList<QContactId> idsToRemove;
        for (int j = 0; j < td.size(); ++j) {
            idsToRemove.append(td.at(j).id());
        }
#else
        QList<QContactLocalId> idsToRemove;
        for (int j = 0; j < td.size(); ++j) {
            idsToRemove.append(td.at(j).localId());
        }
#endif
        syncTimer.start();
        manager.removeContacts(idsToRemove);
        ste = syncTimer.elapsed();
        qDebug() << "    removing test data took" << ste << "milliseconds (" << ((1.0 * ste) / (1.0 * td.size())) << "msec per contact )";
    }

    return 0;
}
