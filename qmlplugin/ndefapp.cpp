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

#include "ndefapp.h"

#include <QtCore/QBitArray>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>

#ifdef DEBUG
#  define DBG(x) qDebug() << x
#else
#  define DBG(x) ((void)0)
#endif
#define WARN(x) qWarning() << x

#define BYTE_ARRAY(a) QByteArray((const char*)&((a)[0]), sizeof(a))

// ==========================================================================
//
// [NFCForum-TS-Type-4-Tag_2.0]
//
// Data Structure of the Capability Container File:
//
// +------------------------------------------------------------------------+
// | Offset | Size | Description                                            |
// +--------+------+--------------------------------------------------------+
// | 0      | 2    | CCLEN (total length, 0x000F-0xFFFE bytes)              |
// | 2      | 1    | Mapping Version (major/minor 4 bits each)              |
// | 3      | 2    | MLe (Maximum R-APDU data size, 0x000F..0xFFFF bytes)   |
// | 5      | 2    | MLc (Maximum C-APDU data size, 0x0001..0xFFFF bytes)   |
// | 7      | 8    | NDEF File Control TLV (see below)                      |
// | 15     | -    | Zero, one, or more TLV blocks                          |
// +------------------------------------------------------------------------+
//
// NDEF File Control TLV:
//
// +------------------------------------------------------------------------+
// | Offset | Size | Description                                            |
// +--------+------+--------------------------------------------------------+
// | 0      | 1    | T = 4                                                  |
// | 1      | 1    | L = 6                                                  |
// | 2      | 2    | File Identifier                                        |
// | 4      | 2    | Maximum NDEF file size, 0x0005..0xFFFE                 |
// | 6      | 1    | NDEF file read access condition (0x00)                 |
// | 7      | 1    | NDEF file write access condition (0x00|0xFF)           |
// +------------------------------------------------------------------------+
//
// Data Structure of the NDEF File:
//
// +------------------------------------------------------------------------+
// | Offset | Size | Description                                            |
// +--------+------+--------------------------------------------------------+
// | 0      | 2    | N = NDEF message size (big-endian)                     |
// | 2      | N    | NDEF message                                           |
// +------------------------------------------------------------------------+
//
// ==========================================================================
static const uchar aid[] = { 0xd2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };
static const uchar cc_ef[] = { 0xe1, 0x03 };
static const uchar cc_data_template[] = {
    0x00, 0x0f, 0x20, 0xff, 0xff, 0xff, 0xff,      /* CC header 7 bytes */
    0x04, 0x06, 0xe1, 0x04, 0x00, 0x00, 0x00, 0xff /* NDEF File Control TLV */
                /*  fid */  /* size */
};
#define CC_NDEF_TLV_OFFSET  (7)
#define CC_NDEF_FID_OFFSET  (CC_NDEF_TLV_OFFSET + 2)
#define CC_NDEF_SIZE_OFFSET (CC_NDEF_TLV_OFFSET + 4)
#define CC_NDEF_FID_SIZE    (2)

#define ISO_CLA (0x00)
#define ISO_INS_SELECT (0xa4)
#define ISO_INS_READ_BINARY (0xb0)
#define ISO_P1_SELECT_BY_ID (0x00)
#define ISO_P2_SELECT_FILE_FIRST (0x00)
#define ISO_P2_RESPONSE_NONE (0x0c)

#define MAX_NDEF_FILE_SIZE (0xfffe)
#define MAX_NDEF_MESSAGE_SIZE (MAX_NDEF_FILE_SIZE - 2)

// 9000 - Normal processing
#define RESP_OK 0x90, 0x00

// ==========================================================================
// NdefApp::Response
// ==========================================================================

class NdefApp::Response
{
    uchar iSw[2];
    QByteArray iData;
    uint iResponseId;

public:
    Response(uchar aSw1, uchar aSw2, const QByteArray& aData = QByteArray()) :
        iSw{aSw1, aSw2},
        iData(aData),
        iResponseId(nextId())
    {}

    Response() :
        iSw{0x6f, 0x00}, // 6F00 - Failure (No precise diagnosis)
        iResponseId(0)
    {}

    uint id() const
    {
        return iResponseId;
    }

    QVariantList toVariantList()
    {
        QVariantList list;
        list << iData                            // response
             << qVariantFromValue<uchar>(iSw[0]) // SW1
             << qVariantFromValue<uchar>(iSw[1]) // SW2
             << iResponseId;                     // response_id
        return list;
    }

private:
    static uint nextId()
    {
        static uint lastId = 0;
        while (!++lastId);
        return lastId;
    }
};

// ==========================================================================
// NdefApp::File
// ==========================================================================

class NdefApp::File
{
public:
    File();
    File(QString, const QByteArray&, const QByteArray&);

    bool isValid() const;
    bool isFullyRead() const;
    uint size() const;
    uint bytesRead() const;
    void reset();
    void confirmRead();
    QString name() const;
    QByteArray read(uint, uint);

private:
    QString iName;
    QByteArray iFid;
    QByteArray iData;
    QBitArray iBytesRead;   // One bit per byte in iData
    int iLastReadStart;     // Inclusive
    int iLastReadEnd;       // Exclusive
};

NdefApp::File::File(
    QString aName,
    const QByteArray& aFid,
    const QByteArray& aData) :
    iName(aName),
    iFid(aFid),
    iData(aData),
    iBytesRead(aData.size()),
    iLastReadStart(0),
    iLastReadEnd(0)
{}

NdefApp::File::File() :
    iLastReadStart(0),
    iLastReadEnd(0)
{}

bool
NdefApp::File::isValid() const
{
    return !iFid.isEmpty();
}

bool
NdefApp::File::isFullyRead() const
{
    return bytesRead() == size();
}

QString
NdefApp::File::name() const
{
    return iName;
}

uint
NdefApp::File::size() const
{
    return iData.size();
}

uint
NdefApp::File::bytesRead() const
{
    return iBytesRead.count(true);
}

void
NdefApp::File::reset()
{
    iBytesRead.fill(false);
    iLastReadStart = iLastReadEnd = 0;
}

void
NdefApp::File::confirmRead()
{
    iBytesRead.fill(true, iLastReadStart, iLastReadEnd);
    iLastReadStart = iLastReadEnd = 0;
    DBG(bytesRead() << "bytes out of" << size());
}

QByteArray
NdefApp::File::read(
    uint aOffset,
    uint aExpected)
{
    uint off = qMin(uint(iData.size()), aOffset);
    uint len = aExpected ? aExpected : (iData.size() - off);

    DBG("Reading [" << off << ".." << (off + len - 1) << "] from" << iName);
    iLastReadEnd = (iLastReadStart = off) + len;
    return iData.mid(off, len);
}

// ==========================================================================
// NdefApp::Private
// ==========================================================================

class NdefApp::Private :
    public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.nfc.LocalHostApp")
    Q_CLASSINFO("D-Bus Introspection",
        "<interface name=\"org.sailfishos.nfc.LocalHostApp\">\n"
        "  <method name=\"GetInterfaceVersion\">\n"
        "    <arg name=\"version\" type=\"i\" direction=\"out\"/>\n"
        "  </method>\n"
        "  <method name=\"Start\">\n"
        "    <arg name=\"host\" type=\"o\" direction=\"in\"/>\n"
        "  </method>\n"
        "  <method name=\"Restart\">\n"
        "    <arg name=\"host\" type=\"o\" direction=\"in\"/>\n"
        "  </method>\n"
        "  <method name=\"Stop\">\n"
        "    <arg name=\"path\" type=\"o\" direction=\"in\"/>\n"
        "  </method>\n"
        "  <method name=\"ImplicitSelect\">\n"
        "    <arg name=\"host\" type=\"o\" direction=\"in\"/>\n"
        "  </method>\n"
        "  <method name=\"Select\">\n"
        "    <arg name=\"host\" type=\"o\" direction=\"in\"/>\n"
        "  </method>\n"
        "  <method name=\"Deselect\">\n"
        "    <arg name=\"path\" type=\"o\" direction=\"in\"/>\n"
        "  </method>\n"
        "  <method name=\"Process\">\n"
        "    <arg name=\"host\" type=\"o\" direction=\"in\"/>\n"
        "    <arg name=\"CLA\" type=\"y\" direction=\"in\"/>\n"
        "    <arg name=\"INS\" type=\"y\" direction=\"in\"/>\n"
        "    <arg name=\"P1\" type=\"y\" direction=\"in\"/>\n"
        "    <arg name=\"P2\" type=\"y\" direction=\"in\"/>\n"
        "    <arg name=\"data\" type=\"ay\" direction=\"in\"/>\n"
        "    <arg name=\"Le\" type=\"u\" direction=\"in\"/>\n"
        "    <arg name=\"response\" type=\"ay\" direction=\"out\"/>\n"
        "    <arg name=\"SW1\" type=\"y\" direction=\"out\"/>\n"
        "    <arg name=\"SW2\" type=\"y\" direction=\"out\"/>\n"
        "    <arg name=\"response_id\" type=\"u\" direction=\"out\"/>\n"
        "  </method>\n"
        "  <method name=\"ResponseStatus\">\n"
        "    <arg name=\"response_id\" type=\"u\" direction=\"in\"/>\n"
        "    <arg name=\"ok\" type=\"b\" direction=\"in\"/>\n"
        "  </method>\n"
        "</interface>\n")

    enum { INTERFACE_VERSION = 1 };
    static const QString NFC_SERVICE_NAME;
    static const QString NFC_SERVICE_INTERFACE;
    static const QString NFC_SERVICE_PATH;
    static const QString APP_PATH;

public:
    Private(const void*, uint, NdefApp*);
    ~Private();

    bool isTooMuchData() const;

public Q_SLOTS:
    int GetInterfaceVersion();
    void Start(QDBusObjectPath);
    void Restart(QDBusObjectPath);
    void Stop(QDBusObjectPath);
    void ImplicitSelect(QDBusObjectPath);
    void Select(QDBusObjectPath);
    void Deselect(QDBusObjectPath);
    void Process(QDBusObjectPath, uchar, uchar, uchar, uchar,  QByteArray, uint, QDBusMessage);
    void ResponseStatus(uint, bool);

private Q_SLOTS:
    void onRegisterLocalHostAppFinished(QDBusPendingCallWatcher*);
    void onRequestModeFinished(QDBusPendingCallWatcher*);
    void onRequestTechsFinished(QDBusPendingCallWatcher*);

private:
    static QDBusMessage createMethodCall(QString);
    static QByteArray ccFileData(uint);
    static QByteArray ndefFileData(const void*, uint);
    NdefApp* parentObject();
    Response select(uchar, uchar,  const QByteArray&);
    Response readBinary(uchar, uchar, uint);
    void mayBeReset();
    void mayBeDone();

public:
    QMap<QByteArray,File> iFiles;
    File* iNdefFile;
    File* iSelectedFile;
    uint iLastReadId;
    bool iDone;
    bool iRegisteredApp;
    uint iRegisteredModeId;
    uint iRegisteredTechsId;
    bool iReady;
    QDBusConnection iBus;
    bool iRegisteredObject;
};

const QString NdefApp::Private::APP_PATH("/ndefshare");
const QString NdefApp::Private::NFC_SERVICE_NAME("org.sailfishos.nfc.daemon");
const QString NdefApp::Private::NFC_SERVICE_INTERFACE("org.sailfishos.nfc.Daemon");
const QString NdefApp::Private::NFC_SERVICE_PATH("/");

NdefApp::Private::Private(
    const void* aNdefData,
    uint aNdefSize,
    NdefApp* aApp) :
    QDBusAbstractAdaptor(aApp),
    iSelectedFile(Q_NULLPTR),
    iLastReadId(0),
    iDone(false),
    iRegisteredApp(false),
    iRegisteredModeId(0),
    iRegisteredTechsId(0),
    iReady(false),
    iBus(QDBusConnection::systemBus()),
    iRegisteredObject(iBus.registerObject(APP_PATH, this,
        QDBusConnection::ExportAllSlots))
{
    // Set files (CC and NDEF)
    QByteArray idCc((char*)cc_ef, sizeof(cc_ef));
    QByteArray idNdef((char*)(cc_data_template + CC_NDEF_FID_OFFSET), CC_NDEF_FID_SIZE);
    iFiles.insert(idCc, File("CC", idCc, ccFileData(aNdefSize)));
    iFiles.insert(idNdef, File("NDEF", idCc, ndefFileData(aNdefData, aNdefSize)));
    iNdefFile = &iFiles[idNdef];

    // If the message is too large, we deliberately leave the object
    // in a non-ready state.
    if (!isTooMuchData()) {
        // Go through the asynchronous sequence:
        //
        // 1. RegisterLocalHostApp("/ndefshare")
        // 2. RequestMode(CardEmulation)
        // 3. RequestTechs(NFC-A)
        //
        // The sequence can be aborted at any point.
        //
        // <method name="RegisterLocalHostApp">
        //   <arg name="path" type="o" direction="in"/>
        //   <arg name="name" type="s" direction="in"/>
        //   <arg name="aid" type="ay" direction="in"/>
        //   <arg name="flags" type="u" direction="in"/>
        // </method>
        //
        // Flags:
        //   0x01 - Allow implicit selection
        QDBusMessage msg(createMethodCall("RegisterLocalHostApp"));
        msg << QVariant::fromValue(QDBusObjectPath(APP_PATH)) // path
            << QString("NfcShare")                            // name
            << BYTE_ARRAY(aid)                                // aid
            << uint(0x01);                                    // flags
        connect(new QDBusPendingCallWatcher(iBus.asyncCall(msg), this),
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(onRegisterLocalHostAppFinished(QDBusPendingCallWatcher*)));
    }
}

NdefApp::Private::~Private()
{
    // Undo the initialization sequence:
    if (iRegisteredTechsId) {
        // <method name="ReleaseTechs">
        //   <arg name="id" type="u" direction="in"/>
        // </method>
        QDBusMessage msg(createMethodCall("ReleaseTechs"));
        msg << iRegisteredTechsId;
        iBus.asyncCall(msg);
    }
    if (iRegisteredModeId) {
        // <method name="ReleaseMode">
        //   <arg name="id" type="u" direction="in"/>
        // </method>
        QDBusMessage msg(createMethodCall("ReleaseMode"));
        msg << iRegisteredModeId;
        iBus.asyncCall(msg);
    }
    if (iRegisteredApp) {
        // <method name="UnregisterLocalHostApp">
        //   <arg name="path" type="o" direction="in"/>
        // </method>
        QDBusMessage msg(createMethodCall("UnregisterLocalHostApp"));
        msg << QVariant::fromValue(QDBusObjectPath(APP_PATH)); // path
        iBus.asyncCall(msg);
    }
    if (iRegisteredObject) {
        iBus.unregisterObject(APP_PATH);
    }
}

void
NdefApp::Private::onRegisterLocalHostAppFinished(
    QDBusPendingCallWatcher* aWatcher)
{
    QDBusPendingReply<void> reply(*aWatcher);

    if (reply.isValid()) {
        iRegisteredApp = true;
        DBG("Registered NFS share service at" << APP_PATH);

        // <method name="RequestMode">
        //   <arg name="enable" type="u" direction="in"/>
        //   <arg name="disable" type="u" direction="in"/>
        //   <arg name="id" type="u" direction="out"/>
        // </method>
        //
        // Polling mode bits:
        //   0x01 - P2P Initiator
        //   0x02 - Reader/Writer
        //
        // Listening mode bits:
        //   0x04 - P2P Target
        //   0x08 - Card Emulation
        QDBusMessage msg(createMethodCall("RequestMode"));
        msg << uint(0x08)   // enable Card Emulation
            << uint(0x02);  // disable Reader/Writer mode
        connect(new QDBusPendingCallWatcher(iBus.asyncCall(msg), this),
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(onRequestModeFinished(QDBusPendingCallWatcher*)));
    } else {
        WARN(reply.error());
    }
    aWatcher->deleteLater();
}

void
NdefApp::Private::onRequestModeFinished(
    QDBusPendingCallWatcher* aWatcher)
{
    QDBusPendingReply<uint> reply(*aWatcher);

    if (reply.isValid()) {
        iRegisteredModeId = reply.value();
        DBG("CE mode request" << iRegisteredModeId);

        // <method name="RequestTechs">
        //   <arg name="allow" type="u" direction="in"/>
        //   <arg name="disallow" type="u" direction="in"/>
        //   <arg name="id" type="u" direction="out"/>
        // </method>
        //
        // Tech bits:
        //   0x01 - NFC-A
        //   0x02 - NFC-B
        //   0x04 - NFC-F
        QDBusMessage msg(createMethodCall("RequestTechs"));
        msg << uint(0x01)         // allow NFC-A
            << uint(0xfffffffe);  // disallow everything else
        connect(new QDBusPendingCallWatcher(iBus.asyncCall(msg), this),
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(onRequestTechsFinished(QDBusPendingCallWatcher*)));
    } else {
        WARN(reply.error());
    }
    aWatcher->deleteLater();
}

void
NdefApp::Private::onRequestTechsFinished(
    QDBusPendingCallWatcher* aWatcher)
{
    QDBusPendingReply<uint> reply(*aWatcher);

    if (reply.isValid()) {
        iRegisteredTechsId = reply.value();
        DBG("NFC-A tech request" << iRegisteredTechsId);
        iReady = true;
        Q_EMIT parentObject()->readyChanged();
    } else {
        WARN(reply.error());
    }
    aWatcher->deleteLater();
}

NdefApp*
NdefApp::Private::parentObject()
{
    return qobject_cast<NdefApp*>(parent());
}

//static
QDBusMessage
NdefApp::Private::createMethodCall(
    QString aMethod)
{
    return QDBusMessage::createMethodCall(NFC_SERVICE_NAME,
        NFC_SERVICE_PATH, NFC_SERVICE_INTERFACE, aMethod);
}

bool
NdefApp::Private::isTooMuchData() const
{
    // Empty NDEF file means that the message is too large
    return !iNdefFile->size();
}

//static
QByteArray
NdefApp::Private::ccFileData(
    uint aNdefSize)
{
    QByteArray data((const char*)cc_data_template, sizeof(cc_data_template));
    const int ndefFileLen = aNdefSize + 2; // Extra 2 bytes for the message size

    if (ndefFileLen <= MAX_NDEF_FILE_SIZE) {
        // big-endian
        data[CC_NDEF_SIZE_OFFSET + 0] = (uchar)(ndefFileLen >> 8);
        data[CC_NDEF_SIZE_OFFSET + 1] = (uchar)(ndefFileLen);
    } else {
        WARN("NDEF message too large:" << aNdefSize << "byte(s)");
    }
    return data;
}

//static
QByteArray
NdefApp::Private::ndefFileData(
    const void* aNdefData,
    uint aNdefSize)
{
    QByteArray data;

    // Data Structure of the NDEF File:
    //
    // +--------------------------------------------------------------------+
    // | Offset | Size | Description                                        |
    // +--------+------+----------------------------------------------------+
    // | 0      | 2    | N = NDEF message size (big-endian)                 |
    // | 2      | N    | NDEF message                                       |
    // +--------------------------------------------------------------------+
    if (aNdefSize <= MAX_NDEF_MESSAGE_SIZE) {
        data.reserve(aNdefSize + 2);
        data.append((uchar)(aNdefSize >> 8)); // big-endian
        data.append((uchar)aNdefSize);
        data.append((char*)aNdefData, aNdefSize);
    }
    return data;
}

NdefApp::Response
NdefApp::Private::select(
    uchar aP1,
    uchar aP2,
    const QByteArray& aFid)
{
    if (aP1 == ISO_P1_SELECT_BY_ID &&
        aP2 == (ISO_P2_SELECT_FILE_FIRST | ISO_P2_RESPONSE_NONE) &&
        iFiles.contains(aFid)) {

        iSelectedFile = &iFiles[aFid];
        DBG("Selected" << aFid.toHex().constData() << iSelectedFile->name());
        return Response(RESP_OK);
    } else {
        DBG("Unknown file" << aFid.toHex().constData());
        return Response();
    }
}

NdefApp::Response
NdefApp::Private::readBinary(
    uchar aP1,
    uchar aP2,
    uint aLe)
{
    // If bit 1 of INS is set to 0 and bit 8 of P1 to 0, then P1-P2
    // (fifteen bits) encodes an offset from zero to 32767.
    if (!(aP1 & 0x80) && iSelectedFile) {
        const uint off = ((uint) aP1 << 8)  | aP2;
        const QByteArray data(iSelectedFile->read(off, aLe));

        DBG(data.toHex().constData());
        return Response(RESP_OK, data);
    } else {
        return Response();
    }
}

void
NdefApp::Private::mayBeReset()
{
    if (!iDone && !iNdefFile->isFullyRead() && iNdefFile->bytesRead()) {
        iNdefFile->reset();
        Q_EMIT parentObject()->bytesTransferredChanged();
    }
}

void
NdefApp::Private::mayBeDone()
{
    if (iNdefFile->isFullyRead()) {
        NdefApp* app = parentObject();

        if (!iDone) {
            iDone = true;
            Q_EMIT app->doneChanged();
        }
        Q_EMIT app->done();
    }
}

// org.sailfishos.nfc.LocalHostApp implementation

int
NdefApp::Private::GetInterfaceVersion()
{
    return INTERFACE_VERSION;
}

void
NdefApp::Private::Start(
    QDBusObjectPath aHost)
{
    DBG("Host" << aHost.path() << "has started");
    mayBeReset();
}

void
NdefApp::Private::Restart(
    QDBusObjectPath aHost)
{
    DBG("Host" << aHost.path() << "has been restarted");
    mayBeDone();
    mayBeReset();
}

void
NdefApp::Private::Stop(
    QDBusObjectPath aHost)
{
    DBG("Host" << aHost.path() << "left");
    mayBeDone();
    mayBeReset();
}

void
NdefApp::Private::ImplicitSelect(
    QDBusObjectPath aHost)
{
    DBG("Implicitly selected for" << aHost.path());
}

void
NdefApp::Private::Select(
    QDBusObjectPath aHost)
{
    DBG("Selected for" << aHost.path());
}

void
NdefApp::Private::Deselect(
    QDBusObjectPath aHost)
{
    DBG("Deselected for" << aHost.path());
}

void
NdefApp::Private::Process(
    QDBusObjectPath aHost,
    uchar aCla,
    uchar aIns,
    uchar aP1,
    uchar aP2,
    QByteArray aData,
    uint aLe,
    QDBusMessage aMessage)
{
    Response response;

    DBG("C-APDU from" << aHost.path() << hex << aCla << aIns << aP1 << aP2 <<
        aData.toHex().constData() << aLe);
    if (aCla == ISO_CLA) {
        if (aIns == ISO_INS_SELECT) {
            response = select(aP1, aP2, aData);
        } else if (aIns == ISO_INS_READ_BINARY) {
            response = readBinary(aP1, aP2, aLe);
            iLastReadId = response.id();
        }
    }

    aMessage.setDelayedReply(true);
    iBus.send(aMessage.createReply(response.toVariantList()));
}

void
NdefApp::Private::ResponseStatus(
    uint aResponseId,
    bool aOk)
{
    DBG("Response" << aResponseId << (aOk ? "ok" : "failed"));
    if (aOk && iLastReadId == aResponseId && iSelectedFile) {
        const uint prev = iNdefFile->bytesRead();
        DBG("Read" << aResponseId << "confirmed");
        iSelectedFile->confirmRead();
        if (iNdefFile->bytesRead() > prev) {
            Q_EMIT parentObject()->bytesTransferredChanged();
        }
    }
}

// ==========================================================================
// NdefApp
// ==========================================================================

NdefApp::NdefApp(
    const void* aNdefData,
    uint aNdefSize,
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(aNdefData, aNdefSize, this))
{}

bool
NdefApp::isTooMuchData() const
{
    return iPrivate->isTooMuchData();
}

bool
NdefApp::isReady() const
{
    return iPrivate->iReady;
}

bool
NdefApp::isDone() const
{
    return iPrivate->iDone;
}

uint
NdefApp::getBytesTotal() const
{
    return iPrivate->iNdefFile->size();
}

uint
NdefApp::getBytesTransferred() const
{
    return iPrivate->iNdefFile->bytesRead();
}

#include "ndefapp.moc"
