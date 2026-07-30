/*
 * Copyright (C) 2026 Jolla Mobile Ltd
 * Copyright (C) 2025 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "nfcshare.h"
#include "ndefapp.h"

#include <QtCore/QDebug>
#include <QtCore/QUrl>

#include <ndef_rec.h>

#ifdef DEBUG
#  define DBG(x) qDebug() << x
#else
#  define DBG(x) ((void)0)
#endif
#define WARN(x) qWarning() << x

// ==========================================================================
// NfcShare::Private
// ==========================================================================

class NfcShare::Private
{
public:
    Private();
    ~Private();

public:
    NdefApp* iApp;
    QString iText;
};

NfcShare::Private::Private() :
    iApp(Q_NULLPTR)
{}

NfcShare::Private::~Private()
{
    delete iApp;
}

// ==========================================================================
// NfcShare
// ==========================================================================

NfcShare::NfcShare(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private)
{}

NfcShare::~NfcShare()
{
    delete iPrivate;
}

QString
NfcShare::getText() const
{
    return iPrivate->iText;
}

void
NfcShare::setText(
    QString aText)
{
    if (iPrivate->iText != aText) {
        iPrivate->iText = aText;
        const bool wasTooMuchData = isTooMuchData();
        const bool wasReady = isReady();
        const bool wasDone = isDone();
        const uint prevBytesTotal = getBytesTotal();
        const uint prevBytesTransferred = getBytesTransferred();

        delete iPrivate->iApp;
        iPrivate->iApp = Q_NULLPTR;

        DBG(aText);
        if (!aText.isEmpty()) {
            NdefRec* ndef = Q_NULLPTR;
            const QByteArray utf8(aText.toUtf8());

            // Transform URL into a URI record and everything else
            // into a Text record
            if ((utf8.startsWith("http://") || utf8.startsWith("https://")) &&
                QUrl(aText).isValid()) {
                NdefRecU* uri = ndef_rec_u_new(utf8.constData());

                if (uri) {
                    ndef = &uri->rec;
                }
            } else {
                NdefRecT* text = ndef_rec_t_new(utf8.constData(), Q_NULLPTR);

                if (text) {
                    ndef = &text->rec;
                }
            }

            if (ndef) {
                iPrivate->iApp = new NdefApp(ndef->raw.bytes, ndef->raw.size, this);
                connect(iPrivate->iApp, SIGNAL(readyChanged()), SIGNAL(readyChanged()));
                connect(iPrivate->iApp, SIGNAL(doneChanged()), SIGNAL(doneChanged()));
                connect(iPrivate->iApp, SIGNAL(bytesTransferredChanged()), SIGNAL(bytesTransferredChanged()));
                connect(iPrivate->iApp, SIGNAL(done()), SIGNAL(done()));
                ndef_rec_unref(ndef);
            }
        }

        if (wasTooMuchData != isTooMuchData()) {
            Q_EMIT tooMuchDataChanged();
        }
        if (wasReady != isReady()) {
            Q_EMIT readyChanged();
        }
        if (wasDone != isDone()) {
            Q_EMIT doneChanged();
        }
        if (prevBytesTotal != getBytesTotal()) {
            Q_EMIT bytesTotalChanged();
        }
        if (prevBytesTransferred != getBytesTransferred()) {
            Q_EMIT bytesTransferredChanged();
        }
        Q_EMIT textChanged();
    }
}

bool
NfcShare::isTooMuchData() const
{
    return iPrivate->iApp && iPrivate->iApp->isTooMuchData();
}

bool
NfcShare::isReady() const
{
    return iPrivate->iApp && iPrivate->iApp->isReady();
}

bool
NfcShare::isDone() const
{
    return iPrivate->iApp && iPrivate->iApp->isDone();
}

uint
NfcShare::getBytesTotal() const
{
    return iPrivate->iApp ? iPrivate->iApp->getBytesTotal() : 0;
}

uint
NfcShare::getBytesTransferred() const
{
    return iPrivate->iApp ? iPrivate->iApp->getBytesTransferred() : 0;
}
