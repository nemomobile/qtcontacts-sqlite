#include "sqlproxy.hpp"

#include <QMutex>
#include <QMap>

// static QMutex mu;
// static QMap<QSqlQuery*, QString> prepared;
// static QMap<QSqlQuery*, QString> bound;

bool qexec(QSqlQuery &q, const QString & query)
{
    qWarning() << "QUERY:EXEC" << ::getpid() << query;
    return q.exec(query);
}

bool qexec(QSqlQuery &q)
{
    qWarning() << "QUERY:EXEC_PRERARED" << ::getpid();
    return q.exec();
}

bool qprepare(QSqlQuery &q, const QString & query)
{
    qWarning() << "QUERY:PREPARE" << ::getpid() << query;
    return q.prepare(query);
}

// void addBindValue(const QVariant & val, QSql::ParamType paramType = QSql::In)
// {
//     if (paramType = QSql::In)
//         qWarning() << "QUERY BIND" << val;
// }

// void bindValue(const QString & placeholder, const QVariant & val, QSql::ParamType paramType = QSql::In)
// {
//     if (paramType = QSql::In)
//         qWarning() << "QUERY BIND P" << placeholder << val;
// }

// void bindValue(int pos, const QVariant & val, QSql::ParamType paramType = QSql::In)
// {
//     if (paramType = QSql::In)
//         qWarning() << "QUERY BIND P" << pos << val;
// }
