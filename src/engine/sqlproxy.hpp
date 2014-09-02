#ifndef _SQLPROXY_HPP_
#define _SQLPROXY_HPP_

#include <QSqlQuery>
#include <QDebug>
#include <sys/types.h>
#include <unistd.h>

bool qexec(QSqlQuery &q, const QString & query);
bool qexec(QSqlQuery &q);
bool qprepare(QSqlQuery &q, const QString & query);
static void qaddbind(QSqlQuery &q, const QVariant & val, QSql::ParamType paramType = QSql::In)
{
    q.addBindValue(val, paramType);
    // if (paramType = QSql::In)
    //     qWarning() << "QUERY BIND" << val;
}


#endif // _SQLPROXY_HPP_
