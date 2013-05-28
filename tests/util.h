/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Mobility Components.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QTCONTACTS_SQLITE_UTIL_H
#define QTCONTACTS_SQLITE_UTIL_H

#include <QtTest/QtTest>

#include <QtGlobal>
#include <QtCore/qnumeric.h>

#ifdef USING_QTPIM
#include <QtContacts>
#else
#include "qtcontacts.h"
#endif

#include "contactid_p.h"

// qtpim Contacts does not support all the feaures of QtMobility Contacts
#ifndef USING_QTPIM
#define DETAIL_DEFINITION_SUPPORTED
//#define MUTABLE_SCHEMA_SUPPORTED // Not supported by qtcontacts-sqlite with QtMobility, either
#define COMPATIBLE_CONTACT_SUPPORTED
#define DISPLAY_LABEL_SUPPORTED
#define CUSTOM_LABEL_SUPPORTED
#else
#endif

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

#define QCONTACTMANAGER_REMOVE_VERSIONS_FROM_URI(params)  params.remove(QString::fromLatin1(QTCONTACTS_VERSION_NAME)); \
                                                          params.remove(QString::fromLatin1(QTCONTACTS_IMPLEMENTATION_VERSION_NAME))

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
                foreach (QContactIdType __apiId, __arguments.first().value<QList<QContactIdType> >()) { \
                    QVERIFY(ContactId::isValid(__apiId)); \
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

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#define SKIP_TEST(x,y) QSKIP(x)
#else
#define SKIP_TEST(x,y) QSKIP(x, y)
#endif


USE_CONTACTS_NAMESPACE

#ifdef USING_QTPIM
Q_DECLARE_METATYPE(QList<QContactId>)
#else
Q_DECLARE_METATYPE(QList<QContactLocalId>)
#endif

void registerIdType()
{
#ifdef USING_QTPIM
    qRegisterMetaType<QContactId>("QContactId");
    qRegisterMetaType<QList<QContactId> >("QList<QContactId>");
#else
    qRegisterMetaType<QContactLocalId>("QContactLocalId");
    qRegisterMetaType<QList<QContactLocalId> >("QList<QContactLocalId>");
#endif
}

#ifdef USING_QTPIM
const char *contactsAddedSignal = SIGNAL(contactsAdded(QList<QContactId>));
const char *contactsChangedSignal = SIGNAL(contactsChanged(QList<QContactId>));
const char *contactsRemovedSignal = SIGNAL(contactsRemoved(QList<QContactId>));
const char *relationshipsAddedSignal = SIGNAL(relationshipsAdded(QList<QContactId>));
const char *relationshipsRemovedSignal = SIGNAL(relationshipsRemoved(QList<QContactId>));
const char *selfContactIdChangedSignal = SIGNAL(selfContactIdChanged(QContactId,QContactId));
#else
const char *contactsAddedSignal = SIGNAL(contactsAdded(QList<QContactLocalId>));
const char *contactsChangedSignal = SIGNAL(contactsChanged(QList<QContactLocalId>));
const char *contactsRemovedSignal = SIGNAL(contactsRemoved(QList<QContactLocalId>));
const char *relationshipsAddedSignal = SIGNAL(relationshipsAdded(QList<QContactLocalId>));
const char *relationshipsRemovedSignal = SIGNAL(relationshipsRemoved(QList<QContactLocalId>));
const char *selfContactIdChangedSignal = SIGNAL(selfContactIdChanged(QContactLocalId,QContactLocalId));
#endif

#ifdef USING_QTPIM
const QContactId &retrievalId(const QContactId &id) { return id; }
#else
QContactLocalId retrievalId(const QContactId &id) { return id.localId(); }
#endif

#ifdef USING_QTPIM
QContactId retrievalId(const QContact &contact)
#else
QContactLocalId retrievalId(const QContact &contact)
#endif
{
    return retrievalId(contact.id());
}

#ifdef USING_QTPIM
QContactId removalId(const QContact &contact) { return retrievalId(contact); }
#else
QContactLocalId removalId(const QContact &contact) { return retrievalId(contact); }
#endif

#ifdef USING_QTPIM
typedef QList<QContactDetail::DetailType> DetailList;
#else
typedef QStringList DetailList;
#endif

DetailList::value_type detailType(const QContactDetail &detail)
{
#ifdef USING_QTPIM
    return detail.type();
#else
    return detail.definitionName();
#endif
}

template<typename T>
DetailList::value_type detailType()
{
#ifdef USING_QTPIM
    return T::Type;
#else
    return T::DefinitionName;
#endif
}

QString detailTypeName(const QContactDetail &detail)
{
#ifdef USING_QTPIM
    // We could create the table to print this, but I'm not bothering now...
    return QString::number(detail.type());
#else
    return detail.definitionName();
#endif
}

#ifdef USING_QTPIM
bool validDetailType(QContactDetail::DetailType type) { return (type != QContactDetail::TypeUndefined); }
#else
bool validDetailType(const QString &type) { return !type.isEmpty(); }
#endif

bool validDetailType(const QContactDetail &detail)
{
#ifdef USING_QTPIM
    return validDetailType(detail.type());
#else
    return validDetailType(detail.definitionName());
#endif
}

#ifdef USING_QTPIM
typedef QMap<int, QVariant> DetailMap;
#else
typedef QVariantMap DetailMap;
#endif

DetailMap detailValues(const QContactDetail &detail)
{
#ifdef USING_QTPIM
    return detail.values();
#else
    return detail.variantValues();
#endif
}

bool validContactType(const QContact &contact)
{
#ifdef USING_QTPIM
    return (contact.type() == QContactType::TypeContact);
#else
    return !contact.type().isEmpty();
#endif
}

template<typename T, typename F>
void setFilterDetail(QContactDetailFilter &filter, F field)
{
#ifdef USING_QTPIM
    filter.setDetailType(T::Type, field);
#else
    filter.setDetailDefinitionName(T::DefinitionName, field);
#endif
}

template<typename T, typename F>
void setFilterDetail(QContactDetailFilter &filter, T type, F field)
{
#ifdef USING_QTPIM
    filter.setDetailType(type, field);
#else
    filter.setDetailDefinitionName(type, field);
#endif
}

template<typename T, typename F>
void setFilterDetail(QContactDetailRangeFilter &filter, F field)
{
#ifdef USING_QTPIM
    filter.setDetailType(T::Type, field);
#else
    filter.setDetailDefinitionName(T::DefinitionName, field);
#endif
}

template<typename T, typename F>
void setFilterDetail(QContactDetailRangeFilter &filter, T type, F field)
{
#ifdef USING_QTPIM
    filter.setDetailType(type, field);
#else
    filter.setDetailDefinitionName(type, field);
#endif
}

template<typename T>
void setFilterDetail(QContactDetailFilter &filter)
{
#ifdef USING_QTPIM
    filter.setDetailType(T::Type);
#else
    filter.setDetailDefinitionName(T::DefinitionName);
#endif
}

template<typename T>
void setFilterValue(QContactDetailFilter &filter, T value)
{
#ifdef USING_QTPIM
    filter.setValue(value);
#else
    filter.setValue(QString(QLatin1String(value)));
#endif
}

template<typename T, typename F>
void setSortDetail(QContactSortOrder &sort, F field)
{
#ifdef USING_QTPIM
    sort.setDetailType(T::Type, field);
#else
    sort.setDetailDefinitionName(T::DefinitionName, field);
#endif
}

template<typename T, typename F>
void setSortDetail(QContactSortOrder &sort, T type, F field)
{
#ifdef USING_QTPIM
    sort.setDetailType(type, field);
#else
    sort.setDetailDefinitionName(type, field);
#endif
}

#ifdef USING_QTPIM
template<typename F>
QString relationshipString(F fn) { return fn(); }
#else
const QString &relationshipString(const QString &s) { return s; }
#endif

template<typename T>
void setFilterType(QContactRelationshipFilter &filter, T type)
{
    filter.setRelationshipType(relationshipString(type));
}

void setFilterContact(QContactRelationshipFilter &filter, const QContact &contact)
{
#ifdef USING_QTPIM
    filter.setRelatedContact(contact);
#else
    filter.setRelatedContactId(contact.id());
#endif
}

QContactRelationship makeRelationship(const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship;

#ifdef USING_QTPIM
    QContact first, second;
    first.setId(firstId);
    second.setId(secondId);
    relationship.setFirst(first);
    relationship.setSecond(second);
#else
    relationship.setFirst(firstId);
    relationship.setSecond(secondId);
#endif

    return relationship;
}

template<typename T>
QContactRelationship makeRelationship(T type, const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship(makeRelationship(firstId, secondId));
    relationship.setRelationshipType(relationshipString(type));
    return relationship;
}

QContactRelationship makeRelationship(const QString &type, const QContactId &firstId, const QContactId &secondId)
{
    QContactRelationship relationship(makeRelationship(firstId, secondId));
    relationship.setRelationshipType(type);
    return relationship;
}

#ifdef USING_QTPIM
const QContact &relatedContact(const QContact &contact) { return contact; }
#else
QContactId relatedContact(const QContact &contact) { return contact.id(); }
#endif

#ifdef USING_QTPIM
QContactId relatedContactId(const QContact &contact) { return contact.id(); }
#else
const QContactId &relatedContactId(const QContactId &id) { return id; }
#endif

#ifdef USING_QTPIM
QList<QContactId> relatedContactIds(const QList<QContact> &contacts)
{
    QList<QContactId> rv;
    foreach (const QContact &contact, contacts) {
        rv.append(contact.id());
    }
    return rv;
}
#else
const QList<QContactId> &relatedContactIds(const QList<QContactId> &ids) { return ids; }
#endif

#endif
