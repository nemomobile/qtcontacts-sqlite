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

#include "semaphore_p.h"

#include <errno.h>
#include <unistd.h>

#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>

#include <QDebug>

namespace {

// Defined as required for ::semun
union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
    struct seminfo  *__buf;
};

}

Semaphore::Semaphore(const char *id, int initial)
    : m_identifier(id)
    , m_initialValue(-1)
    , m_id(-1)
{
    key_t key = ::ftok(m_identifier, 0);

    m_id = ::semget(key, 1, 0);
    if (m_id == -1) {
        if (errno != ENOENT) {
            error("Unable to get semaphore", errno);
        } else {
            // The semaphore does not currently exist
            m_id = ::semget(key, 1, IPC_CREAT | IPC_EXCL | S_IRWXU);
            if (m_id == -1) {
                if (errno == EEXIST) {
                    // Someone else won the race to create the semaphore - retry get 
                    m_id = ::semget(key, 1, 0);
                }

                if (m_id == -1) {
                    error("Unable to create semaphore", errno);
                }
            } else {
                // Set the initial value
                union semun arg = { 0 };
                arg.val = initial;

                int status = ::semctl(m_id, 0, SETVAL, arg);
                if (status == -1) {
                    m_id = -1;
                    error("Unable to initialize semaphore", errno);
                } else {
                    m_initialValue = initial;
                }
            }
        }
    }
}

Semaphore::~Semaphore()
{
}

bool Semaphore::decrement()
{
    if (m_id == -1)
        return false;

    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = -1;
    op.sem_flg = SEM_UNDO;

    if (::semop(m_id, &op, 1) == 0)
        return true;

    error("Unable to decrement semaphore", errno);
    return false;
}

bool Semaphore::increment()
{
    if (m_id == -1)
        return false;

    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = 1;
    op.sem_flg = SEM_UNDO;

    if (::semop(m_id, &op, 1) == 0)
        return true;

    error("Unable to increment semaphore", errno);
    return false;
}

int Semaphore::value() const
{
    if (m_id == -1)
        return -1;

    return ::semctl(m_id, 0, GETVAL, 0);
}

void Semaphore::error(const char *msg, int error)
{
    qWarning() << QString("%1 %2: %3 (%4)").arg(msg).arg(m_identifier).arg(::strerror(error)).arg(error);
}

