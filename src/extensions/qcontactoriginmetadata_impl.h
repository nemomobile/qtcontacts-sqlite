/*
 * Copyright (C) 2013 Jolla Ltd. <mattthew.vogt@jollamobile.com>
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

#ifndef QCONTACTORIGINMETADATA_IMPL_H
#define QCONTACTORIGINMETADATA_IMPL_H

#include "qcontactoriginmetadata.h"
#include "qtcontacts-extensions.h"

#ifdef USING_QTPIM
QTCONTACTS_USE_NAMESPACE
#else
QTM_USE_NAMESPACE
#endif

void QContactOriginMetadata::setId(const QString &s)
{
    setValue(FieldId, s);
}

QString QContactOriginMetadata::id() const
{
    return value<QString>(FieldId);
}

void QContactOriginMetadata::setGroupId(const QString &s)
{
    setValue(FieldGroupId, s);
}

QString QContactOriginMetadata::groupId() const
{
    return value<QString>(FieldGroupId);
}

void QContactOriginMetadata::setEnabled(bool b)
{
    setValue(FieldEnabled, QLatin1String(b ? "true" : "false"));
}

bool QContactOriginMetadata::enabled() const
{
    return value<bool>(FieldEnabled);
}

QContactDetailFilter QContactOriginMetadata::matchId(const QString &s)
{
    QContactDetailFilter filter;
#ifdef USING_QTPIM
    filter.setDetailType(QContactOriginMetadata::Type, FieldId);
#else
    filter.setDetailDefinitionName(QContactOriginMetadata::DefinitionName, FieldId);
#endif
    filter.setValue(s);
    filter.setMatchFlags(QContactFilter::MatchExactly);
    return filter;
}

QContactDetailFilter QContactOriginMetadata::matchGroupId(const QString &s)
{
    QContactDetailFilter filter;
#ifdef USING_QTPIM
    filter.setDetailType(QContactOriginMetadata::Type, FieldGroupId);
#else
    filter.setDetailDefinitionName(QContactOriginMetadata::DefinitionName, FieldGroupId);
#endif
    filter.setValue(s);
    filter.setMatchFlags(QContactFilter::MatchExactly);
    return filter;
}

#ifdef USING_QTPIM
const QContactDetail::DetailType QContactOriginMetadata::Type(static_cast<QContactDetail::DetailType>(QContactDetail__TypeOriginMetadata));
#else
Q_IMPLEMENT_CUSTOM_CONTACT_DETAIL(QContactOriginMetadata, "TpMetadata");
Q_DEFINE_LATIN1_CONSTANT(QContactOriginMetadata::FieldId, "ContactId");
Q_DEFINE_LATIN1_CONSTANT(QContactOriginMetadata::FieldGroupId, "AccountId");
Q_DEFINE_LATIN1_CONSTANT(QContactOriginMetadata::FieldEnabled, "AccountEnabled");
#endif

#endif
