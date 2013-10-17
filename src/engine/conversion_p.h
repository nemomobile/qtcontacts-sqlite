/*
 * Copyright (C) 2013 Jolla Ltd. <matthew.vogt@jollamobile.com>
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

#ifndef QTCONTACTSSQLITE_CONVERSION_P_H
#define QTCONTACTSSQLITE_CONVERSION_P_H

#ifdef USING_QTPIM

#include <QMap>
#include <QString>

namespace Conversion {

int propertyValue(const QString &name, const QMap<QString, int> &propertyValues);
QList<int> propertyValueList(const QStringList &names, const QMap<QString, int> &propertyValues);

QString propertyName(int value, const QMap<int, QString> &propertyNames);
QStringList propertyNameList(const QList<int> &values, const QMap<int, QString> &propertyNames);

namespace OnlineAccount {

QList<int> subTypeList(const QStringList &names);
QStringList subTypeList(const QList<int> &values);

int protocol(const QString &name);
QString protocol(int type);

}

namespace PhoneNumber {

QList<int> subTypeList(const QStringList &names);
QStringList subTypeList(const QList<int> &values);

}

namespace Address {

QList<int> subTypeList(const QStringList &names);
QStringList subTypeList(const QList<int> &values);

}

namespace Anniversary {

int subType(const QString &name);
QString subType(int value);

}

namespace Url {

int subType(const QString &name);
QString subType(int value);

}

namespace Gender {

int gender(const QString &name);
QString gender(int value);

}

}

#endif

#endif
