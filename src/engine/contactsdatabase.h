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

#ifndef QTCONTACTSSQLITE_CONTACTSDATABASE
#define QTCONTACTSSQLITE_CONTACTSDATABASE

#include <QSqlDatabase>
#include <QContactDetail>

#include <QtGlobal>

class ContactsDatabase
{
public:
    enum Identity {
        SelfContactId
    };

    static QSqlDatabase open(const QString &databaseName);
    static QSqlQuery prepare(const char *statement, const QSqlDatabase &database);

    static QString expandQuery(const QString &queryString, const QVariantList &bindings);
    static QString expandQuery(const QString &queryString, const QMap<QString, QVariant> &bindings);
    static QString expandQuery(const QSqlQuery &query);
};

BEGIN_CONTACTS_NAMESPACE
class QContactTpMetadata : public QContactDetail
{
public:
#ifdef USING_QTPIM
    Q_DECLARE_CUSTOM_CONTACT_DETAIL(QContactTpMetadata)
#else
    Q_DECLARE_CUSTOM_CONTACT_DETAIL(QContactTpMetadata, "TpMetadata")
#endif
};
END_CONTACTS_NAMESPACE


#endif
