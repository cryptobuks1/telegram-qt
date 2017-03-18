/*
   Copyright (C) 2014-2015 Alexandr Akulich <akulichalexander@gmail.com>

   This file is a part of TelegramQt library.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

 */

#include "CTelegramDispatcher.hpp"

#include "TelegramNamespace.hpp"
#include "TelegramNamespace_p.hpp"
#include "CTelegramConnection.hpp"
#include "CTelegramModule.hpp"
#include "CRawStream.hpp"
#include "Utils.hpp"
#include "TelegramUtils.hpp"

using namespace TelegramUtils;

#include <QTimer>

#include <QCryptographicHash>
#include <QDebug>
#include <algorithm>

#ifdef DEVELOPER_BUILD
#include "TLTypesDebug.hpp"
#endif

static const QVector<Telegram::DcOption> s_builtInDcs = QVector<Telegram::DcOption>()
        << Telegram::DcOption(QLatin1String("149.154.175.50") , 443)
        << Telegram::DcOption(QLatin1String("149.154.167.51") , 443)
        << Telegram::DcOption(QLatin1String("149.154.175.100"), 443)
        << Telegram::DcOption(QLatin1String("149.154.167.91") , 443)
        << Telegram::DcOption(QLatin1String("91.108.56.165")  , 443);

static const quint32 s_defaultPingInterval = 15000; // 15 sec

const quint32 secretFormatVersion = 3;
const int s_userTypingActionPeriod = 6000; // 6 sec
const int s_localTypingDuration = 5000; // 5 sec
const int s_localTypingRecommendedRepeatInterval = 400; // (s_userTypingActionPeriod - s_localTypingDuration) / 2. Minus 100 ms for insurance.

static const int s_autoConnectionIndexInvalid = -1; // App logic rely on (s_autoConnectionIndexInvalid + 1 == 0)

static const quint32 s_legacyDcInfoTlType = 0x2ec2a43cu; // Scheme23_DcOption
static const quint32 s_legacyVectorTlType = 0x1cb5c415u; // Scheme23_Vector;

CTelegramDispatcher::CTelegramDispatcher(QObject *parent) :
    QObject(parent),
    m_connectionState(TelegramNamespace::ConnectionStateDisconnected),
    m_appInformation(0),
    m_updatesEnabled(true),
    m_deltaTime(0),
    m_messageReceivingFilterFlags(TelegramNamespace::MessageFlagRead),
    m_acceptableMessageTypes(TelegramNamespace::MessageTypeAll),
    m_autoReconnectionEnabled(false),
    m_pingInterval(s_defaultPingInterval),
    m_mediaDataBufferSize(FileRequestDescriptor::defaultDownloadPartSize()),
    m_initializationState(0),
    m_requestedSteps(0),
    m_wantedActiveDc(0),
    m_autoConnectionDcIndex(s_autoConnectionIndexInvalid),
    m_mainConnection(0),
    m_updateRequestId(0),
    m_updatesStateIsLocked(false),
    m_selfUserId(0),
    m_maxMessageId(0),
    m_fileRequestCounter(0),
    m_typingUpdateTimer(new QTimer(this))
{
    m_typingUpdateTimer->setSingleShot(true);
    connect(m_typingUpdateTimer, SIGNAL(timeout()), SLOT(messageActionTimerTimeout()));
}

CTelegramDispatcher::~CTelegramDispatcher()
{
    closeConnection();
}

void CTelegramDispatcher::plugModule(CTelegramModule *module)
{
    m_modules.append(module);
    module->setDispatcher(this);
}

QVector<Telegram::DcOption> CTelegramDispatcher::builtInDcs()
{
    return s_builtInDcs;
}

quint32 CTelegramDispatcher::defaultPingInterval()
{
    return s_defaultPingInterval;
}

bool CTelegramDispatcher::updatesEnabled() const
{
    return m_updatesEnabled;
}

void CTelegramDispatcher::setUpdatesEnabled(bool enable)
{
    m_updatesEnabled = enable;
}

void CTelegramDispatcher::setAppInformation(const CAppInformation *newAppInfo)
{
    m_appInformation = newAppInfo;
}

qint32 CTelegramDispatcher::localTypingRecommendedRepeatInterval()
{
    return s_localTypingRecommendedRepeatInterval;
}

QString CTelegramDispatcher::selfPhone() const
{
    if (!m_selfUserId || !m_users.value(m_selfUserId)) {
        return QString();
    }

    return m_users.value(m_selfUserId)->phone;
}

quint32 CTelegramDispatcher::selfId() const
{
    return m_selfUserId;
}

quint32 CTelegramDispatcher::maxMessageId() const
{
    if (m_initializationState & StepDialogs) {
        return m_maxMessageId;
    }
    return 0;
}

QVector<quint32> CTelegramDispatcher::contactIdList() const
{
    return m_contactIdList;
}

QVector<quint32> CTelegramDispatcher::chatIdList() const
{
    return m_chatIds;
}

void CTelegramDispatcher::addContacts(const QStringList &phoneNumbers, bool replace)
{
    qDebug() << "addContacts" << maskPhoneNumberList(phoneNumbers);
    if (activeConnection()) {
        TLVector<TLInputContact> contactsVector;
        for (int i = 0; i < phoneNumbers.count(); ++i) {
            TLInputContact contact;
            contact.clientId = i;
            contact.phone = phoneNumbers.at(i);
            contactsVector.append(contact);
        }
        activeConnection()->contactsImportContacts(contactsVector, replace);
    } else {
        qDebug() << Q_FUNC_INFO << "No active connection.";
    }
}

void CTelegramDispatcher::deleteContacts(const QVector<quint32> &userIds)
{
    qDebug() << Q_FUNC_INFO << userIds;

    QVector<TLInputUser> users;
    users.reserve(userIds.count());

    foreach (quint32 userId, userIds) {
        TLInputUser inputUser = userIdToInputUser(userId);
        if (inputUser.tlType != TLValue::InputUserEmpty) {
            users.append(inputUser);
        }
    }

    if (!users.isEmpty()) {
        activeConnection()->contactsDeleteContacts(users);
    }
}

QByteArray CTelegramDispatcher::connectionSecretInfo() const
{
    if (!activeConnection() || activeConnection()->authKey().isEmpty()) {
        return QByteArray();
    }

    QByteArray output;
    CRawStreamEx outputStream(&output, /* write */ true);

    outputStream << secretFormatVersion;
    outputStream << activeConnection()->deltaTime();

    const TLDcOption dcInfo = activeConnection()->dcInfo();
    QByteArray legacyDcHostName;

    outputStream << s_legacyDcInfoTlType;
    outputStream << dcInfo.id;
    outputStream << legacyDcHostName;
    outputStream << dcInfo.ipAddress.toUtf8();
    outputStream << dcInfo.port;
    outputStream << activeConnection()->authKey();
    outputStream << activeConnection()->authId();
    outputStream << activeConnection()->serverSalt();
    outputStream << m_updatesState.pts;
    outputStream << m_updatesState.qts;
    outputStream << m_updatesState.date;

    const quint32 chatIdsVectorSize = m_chatIds.count();

    outputStream << s_legacyVectorTlType;
    outputStream << chatIdsVectorSize;

    for (int i = 0; i < m_chatIds.count(); ++i) {
        outputStream << m_chatIds.at(i);
    }

    return output;
}

void CTelegramDispatcher::setMessageReceivingFilter(TelegramNamespace::MessageFlags flags)
{
    m_messageReceivingFilterFlags = flags;
}

void CTelegramDispatcher::setAcceptableMessageTypes(TelegramNamespace::MessageTypeFlags types)
{
    m_acceptableMessageTypes = types;
}

void CTelegramDispatcher::setAutoReconnection(bool enable)
{
    m_autoReconnectionEnabled = enable;
}

void CTelegramDispatcher::setPingInterval(quint32 ms, quint32 serverDisconnectionAdditionTime)
{
    m_pingInterval = ms;
    if (serverDisconnectionAdditionTime < 500) {
        serverDisconnectionAdditionTime = 500;
    }
    m_pingServerAdditionDisconnectionTime = serverDisconnectionAdditionTime;
}

void CTelegramDispatcher::setMediaDataBufferSize(quint32 size)
{
    if (size % 256) {
        qDebug() << Q_FUNC_INFO << "Unable to set incorrect size" << size << ". The value must be divisible by 1 KB";
        return;
    }

    if (!size) {
        size = FileRequestDescriptor::defaultDownloadPartSize();
    }

    m_mediaDataBufferSize = size;
}

bool CTelegramDispatcher::initConnection(const QVector<Telegram::DcOption> &dcs)
{
    if (!dcs.isEmpty()) {
        m_connectionAddresses = dcs;
    } else {
        m_connectionAddresses = s_builtInDcs;
    }

    initConnectionSharedClear();

    tryNextDcAddress();

    return true;
}

void CTelegramDispatcher::tryNextDcAddress()
{
    if (m_connectionAddresses.isEmpty()) {
        return;
    }

    ++m_autoConnectionDcIndex;

    qDebug() << "CTelegramDispatcher::tryNextBuiltInDcAddress(): Dc index" << m_autoConnectionDcIndex;

    if (m_autoConnectionDcIndex >= m_connectionAddresses.count()) {
        if (m_autoReconnectionEnabled) {
            qDebug() << "CTelegramDispatcher::tryNextBuiltInDcAddress(): Could not connect to any known dc. Reconnection enabled -> wrapping up and tring again.";
            m_autoConnectionDcIndex = 0;
        } else {
            qDebug() << "CTelegramDispatcher::tryNextBuiltInDcAddress(): Could not connect to any known dc. Giving up.";
            setConnectionState(TelegramNamespace::ConnectionStateDisconnected);
            return;
        }
    }

    TLDcOption dcInfo;
    dcInfo.ipAddress = m_connectionAddresses.at(m_autoConnectionDcIndex).address;
    dcInfo.port = m_connectionAddresses.at(m_autoConnectionDcIndex).port;

    clearMainConnection();
    m_mainConnection = createConnection(dcInfo);
    initConnectionSharedFinal();
}

bool CTelegramDispatcher::restoreConnection(const QByteArray &secret)
{
    CRawStreamEx inputStream(secret);

    quint32 format;
    qint32 deltaTime = 0;
    TLDcOption dcInfo;
    QByteArray authKey;
    quint64 authId;
    quint64 serverSalt;

    inputStream >> format;

    if (format > secretFormatVersion) {
        qDebug() << Q_FUNC_INFO << "Unknown format version" << format;
        return false;
    } else {
        qDebug() << Q_FUNC_INFO << "Format version:" << format;
    }

    QByteArray legacySelfPhone;
    TLValue legacyDcInfoTlType;
    QByteArray legacyDcHostName;

    inputStream >> deltaTime;
    inputStream >> legacyDcInfoTlType;

    if (legacyDcInfoTlType != s_legacyDcInfoTlType) {
        qDebug() << Q_FUNC_INFO << "Unexpected dataversion" << format;
        return false;
    }

    QByteArray dcIpAddress;

    inputStream >> dcInfo.id;
    inputStream >> legacyDcHostName; // Not needed anymore
    inputStream >> dcIpAddress;
    dcInfo.ipAddress = QString::fromLatin1(dcIpAddress);
    inputStream >> dcInfo.port;

    qDebug() << Q_FUNC_INFO << dcInfo.ipAddress;

    if (format < 3) {
        inputStream >> legacySelfPhone;
    }

    inputStream >> authKey;

    if (authKey.isEmpty()) {
        qDebug() << Q_FUNC_INFO << "Empty auth key data.";
        return false;
    }

    inputStream >> authId;
    inputStream >> serverSalt;

    initConnectionSharedClear();

    if (format >= 1) {
        inputStream >> m_updatesState.pts;
        inputStream >> m_updatesState.qts;
        inputStream >> m_updatesState.date;
    }

    if (format >= 2) {
        TLValue legacyVectorTlType(TLValue::Vector);
        quint32 chatIdsVectorSize = 0;

        inputStream >> legacyVectorTlType;
        inputStream >> chatIdsVectorSize;

        m_chatIds.resize(chatIdsVectorSize);

        for (int i = 0; i < m_chatIds.count(); ++i) {
            inputStream >> m_chatIds[i];
        }
    }

    m_deltaTime = deltaTime;

    clearMainConnection();
    m_wantedActiveDc = dcInfo.id;
    m_mainConnection = createConnection(dcInfo);
    m_mainConnection->setAuthKey(authKey);
    m_mainConnection->setServerSalt(serverSalt);

    if (m_mainConnection->authId() != authId) {
        qDebug() << Q_FUNC_INFO << "Invalid auth data.";
        return false;
    }

    initConnectionSharedFinal();

    return true;
}

void CTelegramDispatcher::initConnectionSharedClear()
{
    m_autoConnectionDcIndex = s_autoConnectionIndexInvalid;

    m_deltaTime = 0;
    m_updateRequestId = 0;
    m_updatesState.pts = 1;
    m_updatesState.qts = 1;
    m_updatesState.date = 1;
    m_chatIds.clear();
    m_maxMessageId = 0;
}

void CTelegramDispatcher::initConnectionSharedFinal()
{
    m_initializationState = StepFirst;
    m_requestedSteps = 0;
    setConnectionState(TelegramNamespace::ConnectionStateConnecting);
    m_updatesStateIsLocked = false;
    m_selfUserId = 0;

    m_actualState = TLUpdatesState();
    m_mainConnection->connectToDc();
}

void CTelegramDispatcher::closeConnection()
{
    setConnectionState(TelegramNamespace::ConnectionStateDisconnected);

    clearMainConnection();
    clearExtraConnections();

    m_dcConfiguration.clear();
    m_delayedPackages.clear();
    qDeleteAll(m_users);
    m_users.clear();
    m_contactIdList.clear();
    m_requestedFileDescriptors.clear();
    m_fileRequestCounter = 0;
    m_contactsMessageActions.clear();
    m_localMessageActions.clear();
    m_chatIds.clear();
    m_chatInfo.clear();
    m_chatFullInfo.clear();
    m_maxMessageId = 0;
    m_wantedActiveDc = 0;
    m_autoConnectionDcIndex = s_autoConnectionIndexInvalid;
}

void CTelegramDispatcher::requestContactAvatar(quint32 userId)
{
    Telegram::UserInfo info;
    if (!getUserInfo(&info, userId)) {
        return;
    }

    Telegram::RemoteFile location;
    if (!info.getProfilePhoto(&location)) {
        return;
    }

    quint32 requestId = requestFile(&location, /* chunkSize */ 512 * 256); // Set chunkSize to some big number to get the whole avatar at once

    if (!requestId) {
        return;
    }

    m_requestedFileDescriptors[requestId].setUserId(userId);
}

bool CTelegramDispatcher::requestMessageMediaData(quint32 messageId)
{
    Telegram::MessageMediaInfo info;
    if (!getMessageMediaInfo(&info, messageId)) {
        return false;
    }

    Telegram::RemoteFile location;
    if (!info.getRemoteFileInfo(&location)) {
        return false;
    }

    location.d->m_size = info.size();
    quint32 requestId = requestFile(&location, info.size());

    if (!requestId) {
        return false;
    }

    m_requestedFileDescriptors[requestId].setMessageId(messageId);
    return true;
}

quint32 CTelegramDispatcher::requestFile(const Telegram::RemoteFile *file, quint32 chunkSize)
{
    if (!file->isValid()) {
        return 0;
    }

    FileRequestDescriptor request;
    request.setType(FileRequestDescriptor::Download);
    request.setDcId(file->d->m_dcId);
    request.setInputLocation(*file->d->m_inputFileLocation);
    request.setSize(file->d->m_size);
    request.uniqueId = file->getUniqueId();

    if (chunkSize) {
        request.setChunkSize(chunkSize);
    } else {
        request.setChunkSize(m_mediaDataBufferSize);
    }
    return addFileRequest(request);
}

bool CTelegramDispatcher::getMessageMediaInfo(Telegram::MessageMediaInfo *messageInfo, quint32 messageId) const
{
    const TLMessage *message = getMessage(messageId);
    if (!message) {
        qDebug() << Q_FUNC_INFO << "Unknown media message" << messageId;
        return false;
    }

    const TLMessageMedia &media = message->media;
    TLMessageMedia &info = *messageInfo->d;
    info = media;
    return true;
}

bool CTelegramDispatcher::requestHistory(const Telegram::Peer &peer, quint32 offset, quint32 limit)
{
    if (!activeConnection()) {
        return false;
    }

    const TLInputPeer inputPeer = publicPeerToInputPeer(peer);

    if (inputPeer.tlType == TLValue::InputPeerEmpty) {
        qDebug() << Q_FUNC_INFO << "Can not resolve contact" << peer.id;
        return false;
    }

    activeConnection()->messagesGetHistory(inputPeer, /* offsetId */ m_maxMessageId + 1, offset, limit, /* maxId */ 0, /* minId */ 0);

    return true;
}

quint32 CTelegramDispatcher::resolveUsername(const QString &userName)
{
    if (!activeConnection()) {
        return 0;
    }

    foreach (const TLUser *user, m_users) {
        if (user->username == userName) {
            return user->id;
        }
    }

    activeConnection()->contactsResolveUsername(userName);

    return 0;
}

quint32 CTelegramDispatcher::uploadFile(const QByteArray &fileContent, const QString &fileName)
{
    if (!m_mainConnection) {
        qWarning() << Q_FUNC_INFO << "Called without connection";
        return 0;
    }
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << fileName;
#endif
    return addFileRequest(FileRequestDescriptor::uploadRequest(fileContent, fileName, m_mainConnection->dcInfo().id));
}

quint32 CTelegramDispatcher::uploadFile(QIODevice *source, const QString &fileName)
{
    return uploadFile(source->readAll(), fileName);
}

quint64 CTelegramDispatcher::sendMessage(const Telegram::Peer &peer, const QString &message)
{
    if (!activeConnection()) {
        return 0;
    }
    const TLInputPeer inputPeer = publicPeerToInputPeer(peer);

    int actionIndex = -1;

    switch (inputPeer.tlType) {
    case TLValue::InputPeerEmpty:
        qDebug() << Q_FUNC_INFO << "Can not resolve contact" << peer.id;
        return 0;
    case TLValue::InputPeerSelf:
        // Makes sense?
        break;
    case TLValue::InputPeerUser:
        actionIndex = TypingStatus::indexForUser(m_localMessageActions, inputPeer.userId);
        break;
    case TLValue::InputPeerChat:
        actionIndex = TypingStatus::indexForChatAndUser(m_localMessageActions, inputPeer.chatId);
        break;
    default:
        // Invalid InputPeer type
        return 0;
    }

    if (actionIndex >= 0) {
        m_localMessageActions.remove(actionIndex);
    }

    quint64 randomId;
    Utils::randomBytes(&randomId);

    const quint64 rpcMessageId = activeConnection()->sendMessage(inputPeer, message, randomId);
    m_rpcIdToMessageRandomIdMap.insert(rpcMessageId, randomId);

    return randomId;
}

quint64 CTelegramDispatcher::forwardMessage(const Telegram::Peer &peer, quint32 messageId)
{
    if (!activeConnection()) {
        return 0;
    }

    quint64 randomId;
    Utils::randomBytes(&randomId);

    return activeConnection()->messagesForwardMessage(publicPeerToInputPeer(peer), messageId, randomId);
}

quint64 CTelegramDispatcher::sendMedia(const Telegram::Peer &peer, const Telegram::MessageMediaInfo &info)
{
    if (!activeConnection()) {
        return 0;
    }
    const TLInputPeer inputPeer = publicPeerToInputPeer(peer);

    if (inputPeer.tlType == TLValue::InputPeerEmpty) {
        qDebug() << Q_FUNC_INFO << "Can not resolve contact" << peer.id;
        return 0;
    }

    const Telegram::MessageMediaInfo::Private *media = info.d;
    TLInputMedia inputMedia;

    if (media->m_isUploaded) {
        switch (media->tlType) {
        case TLValue::MessageMediaPhoto:
            inputMedia.tlType = TLValue::InputMediaUploadedPhoto;
            break;
        case TLValue::MessageMediaAudio:
            inputMedia.tlType = TLValue::InputMediaUploadedAudio;
            inputMedia.duration = media->audio.duration;
            inputMedia.mimeType = media->audio.mimeType;
            break;
        case TLValue::MessageMediaVideo:
            inputMedia.tlType = TLValue::InputMediaUploadedVideo;
            inputMedia.duration = media->video.duration;
            inputMedia.w = media->video.w;
            inputMedia.h = media->video.h;
            break;
        case TLValue::MessageMediaDocument:
            inputMedia.tlType = TLValue::InputMediaUploadedDocument;
            inputMedia.mimeType = media->document.mimeType;
            inputMedia.attributes = media->document.attributes;
            break;
        default:
            return 0;
            break;
        }
        inputMedia.file = *media->m_inputFile;
        inputMedia.caption = media->caption;
    } else {
        switch (media->tlType) {
        case TLValue::MessageMediaPhoto:
            inputMedia.tlType = TLValue::InputMediaPhoto;
            inputMedia.idInputPhoto.tlType = TLValue::InputPhoto;
            inputMedia.idInputPhoto.id = media->photo.id;
            inputMedia.idInputPhoto.accessHash = media->photo.accessHash;
            break;
        case TLValue::MessageMediaAudio:
            inputMedia.tlType = TLValue::InputMediaAudio;
            inputMedia.idInputAudio.tlType = TLValue::InputAudio;
            inputMedia.idInputAudio.id = media->audio.id;
            inputMedia.idInputAudio.accessHash = media->audio.accessHash;
            break;
        case TLValue::MessageMediaVideo:
            inputMedia.tlType = TLValue::InputMediaVideo;
            inputMedia.idInputVeo.tlType = TLValue::InputVideo;
            inputMedia.idInputVeo.id = media->video.id;
            inputMedia.idInputVeo.accessHash = media->video.accessHash;
            break;
        case TLValue::MessageMediaGeo:
            inputMedia.tlType = TLValue::InputMediaGeoPoint;
            inputMedia.geoPoint.tlType = TLValue::InputGeoPoint;
            inputMedia.geoPoint.longitude = media->geo.longitude;
            inputMedia.geoPoint.latitude = media->geo.latitude;
            break;
        case TLValue::MessageMediaContact:
            inputMedia.tlType = TLValue::InputMediaContact;
            inputMedia.firstName = media->firstName;
            inputMedia.lastName = media->lastName;
            inputMedia.phoneNumber = media->phoneNumber;
            break;
        case TLValue::MessageMediaDocument:
            inputMedia.tlType = TLValue::InputMediaDocument;
            inputMedia.idInputDocument.tlType = TLValue::InputDocument;
            inputMedia.idInputDocument.id = media->document.id;
            inputMedia.idInputDocument.accessHash = media->document.accessHash;
            break;
        default:
            return 0;
            break;
        }
    }

    return activeConnection()->sendMedia(inputPeer, inputMedia);
}

bool CTelegramDispatcher::filterReceivedMessage(quint32 messageFlags) const
{
    return m_messageReceivingFilterFlags & messageFlags;
}

quint64 CTelegramDispatcher::createChat(const QVector<quint32> &userIds, const QString chatName)
{
    if (!activeConnection()) {
        return 0;
    }

    TLVector<TLInputUser> users;
    users.reserve(userIds.count());

    foreach (quint32 userId, userIds) {
        const TLInputUser user = userIdToInputUser(userId);
        users.append(user);
    }

    quint64 apiCallId = activeConnection()->messagesCreateChat(users, chatName);

    return apiCallId;
}

bool CTelegramDispatcher::addChatUser(quint32 chatId, quint32 userId, quint32 forwardMessages)
{
    if (!activeConnection()) {
        return false;
    }

    if (!chatId) {
        return false;
    }

    const TLInputUser inputUser = userIdToInputUser(userId);

    switch (inputUser.tlType) {
    case TLValue::InputUserEmpty:
    case TLValue::InputUserSelf:
        return false;
    default:
        break;
    }

    activeConnection()->messagesAddChatUser(chatId, inputUser, forwardMessages);
    return true;
}

void CTelegramDispatcher::setTyping(const Telegram::Peer &peer, TelegramNamespace::MessageAction publicAction)
{
    if (!activeConnection()) {
        return;
    }

    TLInputPeer inputPeer = publicPeerToInputPeer(peer);

    int actionIndex = -1;

    switch (inputPeer.tlType) {
    case TLValue::InputPeerEmpty:
        qDebug() << Q_FUNC_INFO << "Can not resolve contact" << peer.id;
        return;
    case TLValue::InputPeerSelf:
        // Makes no sense
        return;
    case TLValue::InputPeerUser:
        actionIndex = TypingStatus::indexForUser(m_localMessageActions, inputPeer.userId);
        break;
    case TLValue::InputPeerChat:
        actionIndex = TypingStatus::indexForChatAndUser(m_localMessageActions, inputPeer.chatId);
        break;
    default:
        // Invalid InputPeer type
        return;
    }

    if (actionIndex >= 0) {
        if (m_localMessageActions.at(actionIndex).action == publicAction) {
            return; // Avoid flood
        }
    } else if (publicAction == TelegramNamespace::MessageActionNone) {
        return; // Avoid flood
    }

    const TLValue::Value tlAction = publicMessageActionToTelegramAction(publicAction);

    TLSendMessageAction action;
    action.tlType = tlAction;

    activeConnection()->messagesSetTyping(inputPeer, action);

    if (publicAction == TelegramNamespace::MessageActionNone) {
        m_localMessageActions.remove(actionIndex);
    } else {
        if (actionIndex >= 0) {
            m_localMessageActions[actionIndex].action = publicAction;
        } else {
            TypingStatus status;
            status.action = publicAction;
            if (inputPeer.tlType == TLValue::InputPeerChat) {
                status.chatId = inputPeer.chatId;
            } else {
                status.userId = inputPeer.userId;
            }
            status.typingTime = s_localTypingDuration;

            m_localMessageActions.append(status);
        }

        ensureTypingUpdateTimer(s_localTypingDuration);
    }
}

void CTelegramDispatcher::setMessageRead(const Telegram::Peer &peer, quint32 messageId)
{
    if (!activeConnection()) {
        return;
    }
    const TLInputPeer inputPeer = publicPeerToInputPeer(peer);

    if (inputPeer.tlType != TLValue::InputPeerEmpty) {
        activeConnection()->messagesReadHistory(inputPeer, messageId, /* offset */ 0);
    }
}

void CTelegramDispatcher::setOnlineStatus(bool onlineStatus)
{
    if (!activeConnection()) {
        return;
    }
    activeConnection()->accountUpdateStatus(!onlineStatus); // updateStatus accepts bool "offline"
}

void CTelegramDispatcher::checkUserName(const QString &userName)
{
    if (!activeConnection()) {
        return;
    }
    activeConnection()->accountCheckUsername(userName);
}

void CTelegramDispatcher::setUserName(const QString &newUserName)
{
    if (!activeConnection()) {
        return;
    }
    activeConnection()->accountUpdateUsername(newUserName);
}

QString CTelegramDispatcher::contactAvatarToken(quint32 userId) const
{
    const TLUser *user = m_users.value(userId);

    if (!user) {
        qDebug() << Q_FUNC_INFO << "Unknown identifier" << userId;
        return QString();
    }

    return userAvatarToken(user);
}

QString CTelegramDispatcher::chatTitle(quint32 chatId) const
{
    if (!chatId) {
        return QString();
    }

    if (!m_chatInfo.contains(chatId)) {
        return QString();
    }

    return m_chatInfo.value(chatId).title;
}

bool CTelegramDispatcher::setWantedDc(quint32 dc)
{
    m_wantedActiveDc = dc;
    ensureMainConnectToWantedDc();
    return true;
}

bool CTelegramDispatcher::getUserInfo(Telegram::UserInfo *userInfo, quint32 userId) const
{
    if (!m_users.contains(userId)) {
        qDebug() << Q_FUNC_INFO << "Unknown user" << userId;
        return false;
    }

    const TLUser *user = m_users.value(userId);
    TLUser &info = *userInfo->d;
    info = *user;
    return true;
}

bool CTelegramDispatcher::getChatInfo(Telegram::GroupChat *outputChat, quint32 chatId) const
{
    if (!chatId) {
        return false;
    }

    if (!m_chatInfo.contains(chatId)) {
        return false;
    }

    if (!outputChat) {
        return false;
    }

    const TLChat &chat = m_chatInfo.value(chatId);
    outputChat->id = chatId;
    outputChat->title = chat.title;

    // There can be a mistake in legacy Chat::left flag and participants count correction
    if (m_chatFullInfo.contains(chatId)) {
        const TLChatFull &chatFull = m_chatFullInfo.value(chatId);
        bool haveSelf = false;
        foreach (const TLChatParticipant &participant, chatFull.participants.participants) {
            if (participant.userId == m_selfUserId) {
                haveSelf = true;
                break;
            }
        }

        outputChat->participantsCount = chatFull.participants.participants.count();
        if (!haveSelf) {
            ++outputChat->participantsCount;
        }
    } else {
        outputChat->participantsCount = chat.participantsCount;
    }

    outputChat->date = chat.date;
    outputChat->left = false;

    return true;
}

bool CTelegramDispatcher::getChatParticipants(QVector<quint32> *participants, quint32 chatId)
{
    if (!chatId) {
        return false;
    }

    participants->clear();

    bool needsUpdate = false;
    if (!m_chatFullInfo.contains(chatId)) {
        activeConnection()->messagesGetFullChat(chatId);
        needsUpdate = true;
    }
    if (!m_chatInfo.contains(chatId)) {
        activeConnection()->messagesGetChats(TLVector<quint32>() << chatId);
        needsUpdate = true;
    }

    if (needsUpdate) {
        return true;
    }

    const TLChatFull &fullChat = m_chatFullInfo.value(chatId);
//    const TLChat &chat = m_chatInfo.value(chatId);

    foreach (const TLChatParticipant &participant, fullChat.participants.participants) {
        participants->append(participant.userId);
    }

//    if (!chat.left && !participants->contains(m_selfUserId)) {
    if (!participants->contains(m_selfUserId)) {
        participants->append(m_selfUserId);
    }

    return true;
}

void CTelegramDispatcher::onSelfUserReceived(const TLUser &selfUser)
{
    TLUser *existsUser = m_users.value(selfUser.id);

    if (existsUser) {
        *existsUser = selfUser;
    } else {
        m_users.insert(selfUser.id, new TLUser(selfUser));
    }

    if (m_selfUserId) {
        if (m_selfUserId != selfUser.id) {
            qDebug() << "Got self user with different id.";

            m_selfUserId = selfUser.id;
            emit selfUserAvailable(selfUser.id);
        }
    } else {
        m_selfUserId = selfUser.id;
        emit selfUserAvailable(selfUser.id);
        continueInitialization(StepKnowSelf);
    }
}

void CTelegramDispatcher::onUsersReceived(const QVector<TLUser> &users)
{
    qDebug() << Q_FUNC_INFO << users.count();
    foreach (const TLUser &user, users) {
        TLUser *existsUser = m_users.value(user.id);

        if (existsUser) {
            *existsUser = user;
        } else {
            m_users.insert(user.id, new TLUser(user));
        }

        int indexOfRequest = m_askedUserIds.indexOf(user.id);
        if (indexOfRequest >= 0) {
            m_askedUserIds.remove(indexOfRequest);
        }

        if (!existsUser) {
            emit userInfoReceived(user.id);
        }
    }
}

void CTelegramDispatcher::whenContactListReceived(const QVector<quint32> &contactList)
{
    qDebug() << Q_FUNC_INFO << contactList;

    QVector<quint32> newContactList = contactList;
    std::sort(newContactList.begin(), newContactList.end());

    if (m_contactIdList != newContactList) {
        m_contactIdList = newContactList;
        emit contactListChanged();
    }

    continueInitialization(StepContactList);
}

void CTelegramDispatcher::whenContactListChanged(const QVector<quint32> &added, const QVector<quint32> &removed)
{
    qDebug() << Q_FUNC_INFO << added << removed;
    QVector<quint32> newContactList = m_contactIdList;

    // There is some redundant checks, but let's be paranoid
    foreach (const quint32 contact, added) {
        if (!newContactList.contains(contact)) {
            newContactList.append(contact);
        }
    }

    foreach (const quint32 contact, removed) {
        for (int i = 0; i < newContactList.count(); ++i) {
            // We can use remove one, because we warranty that there is no duplication
#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
            newContactList.removeOne(contact);
#else
            int index = newContactList.indexOf(contact);
            if (index < 0) {
                continue;
            }
            newContactList.remove(index);
#endif
        }
    }

    std::sort(newContactList.begin(), newContactList.end());

    // There is no valid cases when lists are equal, but the check is (usually) cheap.
    if (m_contactIdList != newContactList) {
        m_contactIdList = newContactList;
        emit contactListChanged();
    }
}

void CTelegramDispatcher::messageActionTimerTimeout()
{
    int minTime = s_userTypingActionPeriod;

    for (int i = m_contactsMessageActions.count() - 1; i >= 0; --i) {
        int remainingTime = m_contactsMessageActions.at(i).typingTime - m_typingUpdateTimer->interval();
        if (remainingTime < 15) { // Let 15 ms be allowed correction
            if (m_contactsMessageActions.at(i).chatId) {
                emit contactChatMessageActionChanged(m_contactsMessageActions.at(i).chatId,
                                                    m_contactsMessageActions.at(i).userId,
                                                    TelegramNamespace::MessageActionNone);
            } else {
                emit contactMessageActionChanged(m_contactsMessageActions.at(i).userId,
                                                TelegramNamespace::MessageActionNone);
            }
            m_contactsMessageActions.remove(i);
        } else {
            m_contactsMessageActions[i].typingTime = remainingTime;
            if (minTime > remainingTime) {
                minTime = remainingTime;
            }
        }
    }

    for (int i = m_localMessageActions.count() - 1; i >= 0; --i) {
        int timeRemaining = m_localMessageActions.at(i).typingTime - m_typingUpdateTimer->interval();
        if (timeRemaining < 15) { // Let 15 ms be allowed correction
            m_localMessageActions.remove(i);
        } else {
            m_localMessageActions[i].typingTime = timeRemaining;
            if (minTime > timeRemaining) {
                minTime = timeRemaining;
            }
        }
    }

    if (!m_contactsMessageActions.isEmpty() || !m_localMessageActions.isEmpty()) {
        m_typingUpdateTimer->start(minTime);
    }
}

void CTelegramDispatcher::whenMessagesHistoryReceived(const TLMessagesMessages &messages)
{
    foreach (const TLMessage &message, messages.messages) {
        processMessageReceived(message);
    }
}

void CTelegramDispatcher::onMessagesDialogsReceived(const TLMessagesDialogs &dialogs, quint32 offset, quint32 maxId, quint32 limit)
{
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << dialogs << offset << maxId << limit;
#else
    qDebug() << Q_FUNC_INFO << offset << maxId << limit;
#endif

    onUsersReceived(dialogs.users);
    onChatsReceived(dialogs.chats);

    if (!(m_initializationState & StepDialogs)) {
        if (!dialogs.messages.isEmpty()) {
            m_maxMessageId = dialogs.messages.last().id;
        }

        continueInitialization(StepDialogs);
    }


//    foreach (const TLMessage &message, dialogs.messages) {
//        processMessageReceived(message);
//    }

//    switch (dialogs.tlType) {
//    case TLValue::MessagesDialogs:
//        break;
//    case TLValue::MessagesDialogsSlice:
//        dialogs.count;
//        activeConnection()->messagesGetDialogs();
//        break;
//    default:
//        break;
//    }
}

void CTelegramDispatcher::getDcConfiguration()
{
    activeConnection()->helpGetConfig();
}

void CTelegramDispatcher::getInitialUsers()
{
    TLInputUser selfUser;
    selfUser.tlType = TLValue::InputUserSelf;
    activeConnection()->usersGetUsers(QVector<TLInputUser>() << selfUser);

    TLInputUser telegramUser;
    telegramUser.tlType = TLValue::InputUser;
    telegramUser.userId = 777000;
    activeConnection()->usersGetUsers(QVector<TLInputUser>() << telegramUser);
}

void CTelegramDispatcher::getInitialDialogs()
{
    activeConnection()->messagesGetDialogs(0, 1);
}

void CTelegramDispatcher::getContacts()
{
    activeConnection()->contactsGetContacts(QString()); // Empty hash argument for now.
}

void CTelegramDispatcher::getChatsInfo()
{
    if (m_chatIds.isEmpty()) {
        continueInitialization(StepChatInfo);
    } else {
        activeConnection()->messagesGetChats(m_chatIds);
    }
}

void CTelegramDispatcher::getUpdatesState()
{
    qDebug() << Q_FUNC_INFO;
    m_updatesStateIsLocked = true;
    activeConnection()->updatesGetState();
}

void CTelegramDispatcher::whenUpdatesStateReceived(const TLUpdatesState &updatesState)
{
    m_actualState = updatesState;
    checkStateAndCallGetDifference();
}

// Should be called via checkStateAndCallGetDifference()
void CTelegramDispatcher::getDifference()
{
    activeConnection()->updatesGetDifference(m_updatesState.pts, m_updatesState.date, m_updatesState.qts);
}

void CTelegramDispatcher::whenUpdatesDifferenceReceived(const TLUpdatesDifference &updatesDifference)
{
    switch (updatesDifference.tlType) {
    case TLValue::UpdatesDifference:
    case TLValue::UpdatesDifferenceSlice:
        qDebug() << Q_FUNC_INFO << "UpdatesDifference" << updatesDifference.newMessages.count();
        foreach (const TLChat &chat, updatesDifference.chats) {
            updateChat(chat);
        }

        foreach (const TLMessage &message, updatesDifference.newMessages) {
            if ((message.tlType != TLValue::MessageService) && (filterReceivedMessage(getPublicMessageFlags(message.flags)))) {
                continue;
            }

            processMessageReceived(message);
        }
        if (updatesDifference.tlType == TLValue::UpdatesDifference) {
            setUpdateState(updatesDifference.state.pts, updatesDifference.state.seq, updatesDifference.state.date);
        } else { // UpdatesDifferenceSlice
            // Looks like updatesDifference.intermediateState is always null nowadays.
            setUpdateState(updatesDifference.intermediateState.pts, updatesDifference.intermediateState.seq, updatesDifference.intermediateState.date);
        }

        foreach (const TLUpdate &update, updatesDifference.otherUpdates) {
            processUpdate(update);
        }

        break;
    case TLValue::UpdatesDifferenceEmpty:
        qDebug() << Q_FUNC_INFO << "UpdatesDifferenceEmpty";

        // Try to update actual and local state in this weird case.
        QTimer::singleShot(10, this, SLOT(getUpdatesState()));
        return;
        break;
    default:
        qDebug() << Q_FUNC_INFO << "unknown diff type:" << updatesDifference.tlType.toString();
        break;
    }

    checkStateAndCallGetDifference();
}

void CTelegramDispatcher::onChatsReceived(const QVector<TLChat> &chats)
{
    qDebug() << Q_FUNC_INFO << chats.count();

    foreach (const TLChat &chat, chats) {
        updateChat(chat);
    }

    continueInitialization(StepChatInfo);
}

void CTelegramDispatcher::whenMessagesFullChatReceived(const TLChatFull &chat, const QVector<TLChat> &chats, const QVector<TLUser> &users)
{
    Q_UNUSED(chats);

    onUsersReceived(users);
    updateFullChat(chat);
}

void CTelegramDispatcher::setConnectionState(TelegramNamespace::ConnectionState state)
{
    qDebug() << Q_FUNC_INFO << state;

    if (m_connectionState == state) {
        return;
    }

    m_connectionState = state;
    emit connectionStateChanged(state);
}

quint32 CTelegramDispatcher::addFileRequest(const FileRequestDescriptor &descriptor)
{
    if (!descriptor.isValid()) {
        return 0;
    }

    m_requestedFileDescriptors.insert(++m_fileRequestCounter, descriptor);

    CTelegramConnection *connection = getExtraConnection(descriptor.dcId());

    if (connection->authState() == CTelegramConnection::AuthStateSignedIn) {
        processFileRequestForConnection(connection, m_fileRequestCounter);
    } else {
        ensureSignedConnection(connection);
    }

    return m_fileRequestCounter;
}

void CTelegramDispatcher::processFileRequestForConnection(CTelegramConnection *connection, quint32 requestId)
{
    const FileRequestDescriptor descriptor = m_requestedFileDescriptors.value(requestId);
    qDebug() << Q_FUNC_INFO << requestId << descriptor.type();

    if (connection->authState() != CTelegramConnection::AuthStateSignedIn) {
        qDebug() << "Failed to request file operation" << connection << requestId << connection->authState();
        return;
    }

    switch (descriptor.type()) {
    case FileRequestDescriptor::Download:
        connection->downloadFile(descriptor.inputLocation(), descriptor.offset(), descriptor.chunkSize(), requestId);
        break;
    case FileRequestDescriptor::Upload:
        connection->uploadFile(descriptor.fileId(), descriptor.part(), descriptor.data(), requestId);
        break;
    default:
        break;
    }
}

inline bool ensureDcOption(QVector<TLDcOption> *vector, const TLDcOption &option)
{
    for (int i = 0; i < vector->count(); ++i) {
        if (vector->at(i).id == option.id) {
            vector->replace(i, option);
            return true;
        }
    }

    return false;
}

void CTelegramDispatcher::processUpdate(const TLUpdate &update)
{
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << update;
#endif

    quint32 newPts = m_updatesState.pts;

    switch (update.tlType) {
    case TLValue::UpdateNewMessage:
    case TLValue::UpdateReadMessagesContents:
    case TLValue::UpdateReadHistoryInbox:
    case TLValue::UpdateReadHistoryOutbox:
    case TLValue::UpdateDeleteMessages:
    case TLValue::UpdateWebPage:
    case TLValue::UpdateNewChannelMessage:
    case TLValue::UpdateDeleteChannelMessages:
        if (m_updatesState.pts + update.ptsCount != update.pts) {
            qDebug() << "Need inner updates:" << m_updatesState.pts << "+" << update.ptsCount << "!=" << update.pts;
            qDebug() << "Updates delaying is not implemented yet. Recovery via getDifference() in 10 ms";
            QTimer::singleShot(10, this, SLOT(getDifference()));
            return;
        } else {
            newPts = update.pts;
        }
        break;
    default:
        break;
    }

    switch (update.tlType) {
    case TLValue::UpdateNewMessage:
        qDebug() << Q_FUNC_INFO << "UpdateNewMessage";
        processMessageReceived(update.message);
        break;
    case TLValue::UpdateMessageID:
        updateSentMessageId(update.randomId, update.id);
        break;
//    case TLValue::UpdateReadMessages:
//        foreach (quint32 messageId, update.messages) {
//            const QPair<QString, quint64> phoneAndId = m_messagesMap.value(messageId);
//            emit sentMessageStatusChanged(phoneAndId.first, phoneAndId.second, TelegramNamespace::MessageDeliveryStatusRead);
//        }
//        ensureUpdateState(update.pts);
//        break;
//    case TLValue::UpdateDeleteMessages:
//        update.messages;
//        ensureUpdateState(update.pts);
//        break;
//    case TLValue::UpdateRestoreMessages:
//        update.messages;
//        ensureUpdateState(update.pts);
//        break;
    case TLValue::UpdateUserTyping:
    case TLValue::UpdateChatUserTyping:
        if (m_users.contains(update.userId)) {
            TelegramNamespace::MessageAction action = telegramMessageActionToPublicAction(update.action.tlType);

            int remainingTime = s_userTypingActionPeriod;
            remainingTime += m_typingUpdateTimer->remainingTime();

            int index = -1;
            if (update.tlType == TLValue::UpdateUserTyping) {
                index = TypingStatus::indexForUser(m_contactsMessageActions, update.userId);
                emit contactMessageActionChanged(update.userId, action);
            } else {
                index = TypingStatus::indexForChatAndUser(m_contactsMessageActions, update.chatId, update.userId);
                emit contactChatMessageActionChanged(update.chatId,
                                                    update.userId, action);
            }

            if (index < 0) {
                index = m_contactsMessageActions.count();
                TypingStatus status;
                status.userId = update.userId;
                if (update.tlType == TLValue::UpdateChatUserTyping) {
                    status.chatId = update.chatId;
                }
                m_contactsMessageActions.append(status);
            }

            m_contactsMessageActions[index].action = action;
            m_contactsMessageActions[index].typingTime = remainingTime;

            ensureTypingUpdateTimer(remainingTime);
        }
        break;
    case TLValue::UpdateChatParticipants: {
        TLChatFull newChatState = m_chatFullInfo.value(update.participants.chatId);
        newChatState.id = update.participants.chatId; // newChatState can be newly created empty chat
        newChatState.participants = update.participants;
        updateFullChat(newChatState);

        qDebug() << Q_FUNC_INFO << "chat id resolved to" << update.participants.chatId;
        break;
    }
    case TLValue::UpdateUserStatus: {
        if (update.userId == m_selfUserId) {
            break;
        }

        TLUser *user = m_users.value(update.userId);
        if (user) {
            user->status = update.status;
            emit contactStatusChanged(update.userId, getApiContactStatus(user->status.tlType));
        }
        break;
    }
    case TLValue::UpdateUserName: {
        TLUser *user = m_users.value(update.userId);
        if (user) {
            bool changed = (user->firstName == update.firstName) && (user->lastName == update.lastName);
            if (changed) {
                user->firstName = update.firstName;
                user->lastName = update.lastName;
                user->username = update.username;
                emit contactProfileChanged(update.userId);
            }
        }
        break;
    }
//    case TLValue::UpdateUserPhoto:
//        update.userId;
//        update.date;
//        update.photo;
//        update.previous;
//        break;
//    case TLValue::UpdateContactRegistered:
//        update.userId;
//        update.date;
//        break;
//    case TLValue::UpdateContactLink:
//        update.userId;
//        update.myLink;
//        update.foreignLink;
//        break;
//    case TLValue::UpdateActivation:
//        update.userId;
//        break;
//    case TLValue::UpdateNewAuthorization:
//        update.authKeyId;
//        update.date;
//        update.device;
//        update.location;
//        break;
//    case TLValue::UpdateNewGeoChatMessage:
//        update.message;
//        break;
//    case TLValue::UpdateNewEncryptedMessage:
//        update.message;
//        update.qts;
//        break;
//    case TLValue::UpdateEncryptedChatTyping:
//        update.chatId;
//        break;
//    case TLValue::UpdateEncryption:
//        update.chat;
//        update.date;
//        break;
//    case TLValue::UpdateEncryptedMessagesRead:
//        update.chatId;
//        update.maxDate;
//        update.date;
//        break;
//    case TLValue::UpdateChatParticipantAdd:
//        update.chatId;
//        update.userId;
//        update.inviterId;
//        update.version;
//        break;
//    case TLValue::UpdateChatParticipantDelete:
//        update.chatId;
//        update.userId;
//        update.version;
//        break;
    case TLValue::UpdateDcOptions: {
        int dcUpdatesReplaced = 0;
        int dcUpdatesInserted = 0;
        foreach (const TLDcOption &option, update.dcOptions) {
            if (ensureDcOption(&m_dcConfiguration, option)) {
                ++dcUpdatesReplaced;
            } else {
                ++dcUpdatesInserted;
            }
        }

        qDebug() << Q_FUNC_INFO << "Dc configuration update replaces" << dcUpdatesReplaced << "options (" << dcUpdatesInserted << "options inserted).";
        break;
    }
//    case TLValue::UpdateUserBlocked:
//        update.userId;
//        update.blocked;
//        break;
//    case TLValue::UpdateNotifySettings:
//        update.peer;
//        update.notifySettings;
//        break;
    case TLValue::UpdateReadHistoryInbox:
    case TLValue::UpdateReadHistoryOutbox: {
        Telegram::Peer peer = peerToPublicPeer(update.peer);
        if (!peer.id) {
#ifdef DEVELOPER_BUILD
            qDebug() << Q_FUNC_INFO << update.tlType << "Unable to resolve peer" << update.peer;
#else
            qDebug() << Q_FUNC_INFO << update.tlType << "Unable to resolve peer" << update.peer.tlType << update.peer.userId << update.peer.chatId;
#endif
        }
        if (update.tlType == TLValue::UpdateReadHistoryInbox) {
            emit messageReadInbox(peer, update.maxId);
        } else {
            emit messageReadOutbox(peer, update.maxId);
        }
        break;
    }
    default:
        qDebug() << Q_FUNC_INFO << "Update type" << update.tlType.toString() << "is not implemented yet.";
        break;
    }

    ensureUpdateState(newPts);
}

void CTelegramDispatcher::processMessageReceived(const TLMessage &message)
{
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << message;
#endif
    internalProcessMessageReceived(message);
    ensureMaxMessageId(message.id);
}

void CTelegramDispatcher::internalProcessMessageReceived(const TLMessage &message)
{
    if (message.tlType == TLValue::MessageEmpty) {
        return;
    }

    if (message.tlType == TLValue::MessageService) {
        const TLMessageAction &action = message.action;

        const quint32 chatId = message.toId.chatId;
        TLChat chat = m_chatInfo.value(chatId);
        TLChatFull fullChat = m_chatFullInfo.value(chatId);

        chat.id = chatId;
        fullChat.id = chatId;

        switch (action.tlType) {
        case TLValue::MessageActionChatCreate:
            chat.title = action.title;
            chat.participantsCount = action.users.count();
            updateChat(chat);
            break;
        case TLValue::MessageActionChatAddUser: {
            TLVector<TLChatParticipant> participants = fullChat.participants.participants;
            for (int i = 0; i < participants.count(); ++i) {
                if (participants.at(i).userId == action.userId) {
                    return;
                }
            }

            TLChatParticipant newParticipant;
            newParticipant.userId = action.userId;
            participants.append(newParticipant);

            fullChat.participants.participants = participants;
            chat.participantsCount = participants.count();
            updateChat(chat);
            updateFullChat(fullChat);
            }
            break;
        case TLValue::MessageActionChatDeleteUser: {
            TLVector<TLChatParticipant> participants = fullChat.participants.participants;
            for (int i = 0; i < participants.count(); ++i) {
                if (participants.at(i).userId == action.userId) {
                    participants.remove(i);
                    break;
                }
            }

            fullChat.participants.participants = participants;
            chat.participantsCount = participants.count();
            updateChat(chat);
            updateFullChat(fullChat);
            }
            break;
        case TLValue::MessageActionChatEditTitle:
            chat.title = action.title;
            updateChat(chat);
            break;
        case TLValue::MessageActionChatEditPhoto:
        case TLValue::MessageActionChatDeletePhoto:
            fullChat.chatPhoto = action.photo;
            updateFullChat(fullChat);
            break;
        default:
            break;
        }
        return;
    }

    const TelegramNamespace::MessageType messageType = telegramMessageTypeToPublicMessageType(message.media.tlType);

    if (!(messageType & m_acceptableMessageTypes)) {
        return;
    }

    if (message.media.tlType != TLValue::MessageMediaEmpty) {
        m_knownMediaMessages.insert(message.id, new TLMessage(message));
    }

    Telegram::Message apiMessage;

    TelegramNamespace::MessageFlags messageFlags = getPublicMessageFlags(message.flags);
    if (messageFlags & TelegramNamespace::MessageFlagForwarded) {
        apiMessage.forwardContactId = message.fromId;
        apiMessage.fwdTimestamp = message.fwdDate;
    }

    Telegram::Peer peer;
    if ((message.toId.tlType != TLValue::PeerUser) || (messageFlags & TelegramNamespace::MessageFlagOut)) {
        // To a group chat or an outgoing message
        peer = peerToPublicPeer(message.toId);
    } else {
        // Personal chat from someone
        peer = Telegram::Peer(message.fromId, Telegram::Peer::User);
    }

    if (!peer.isValid()) {
        qWarning() << Q_FUNC_INFO << "Unknown peer type!";
        return;
    }
    apiMessage.setPeer(peer);
    apiMessage.fromId = message.fromId;
    apiMessage.type = messageType;
    apiMessage.text = message.message;
    apiMessage.id = message.id;
    apiMessage.timestamp = message.date;
    apiMessage.flags = messageFlags;

    if (!m_users.contains(apiMessage.fromId) && !m_askedUserIds.contains(apiMessage.fromId)) {
        m_askedUserIds.append(apiMessage.fromId);

        activeConnection()->messagesGetDialogs(0, 1);
    }

    emit messageReceived(apiMessage);
}

void CTelegramDispatcher::emitChatChanged(quint32 id)
{
    if (!m_chatIds.contains(id)) {
        m_chatIds.append(id);

        if (m_updateRequestId) {
            qDebug() << Q_FUNC_INFO << "Chat change is result of creation request:" << m_updateRequestId << id;
            emit createdChatIdReceived(m_updateRequestId, id);
        }

        emit chatAdded(id);
    } else {
        emit chatChanged(id);
    }
}

void CTelegramDispatcher::updateChat(const TLChat &newChat)
{
    if (!m_chatInfo.contains(newChat.id)) {
        m_chatInfo.insert(newChat.id, newChat);
    } else {
        m_chatInfo[newChat.id] = newChat;
    }
    emitChatChanged(newChat.id);
}

void CTelegramDispatcher::updateFullChat(const TLChatFull &newChat)
{
    if (!m_chatFullInfo.contains(newChat.id)) {
        m_chatFullInfo.insert(newChat.id, newChat);
    } else {
        m_chatFullInfo[newChat.id] = newChat;
    }
    emitChatChanged(newChat.id);
}

TLInputPeer CTelegramDispatcher::publicPeerToInputPeer(const Telegram::Peer &peer) const
{
    TLInputPeer inputPeer;

    if (peer.type == Telegram::Peer::Chat) {
        inputPeer.tlType = TLValue::InputPeerChat;
        inputPeer.chatId = peer.id;
        return inputPeer;
    }

    if (peer.id == m_selfUserId) {
        inputPeer.tlType = TLValue::InputPeerSelf;
        return inputPeer;
    }

    inputPeer.tlType = TLValue::InputPeerUser;
    inputPeer.userId = peer.id;

    return inputPeer;
}

Telegram::Peer CTelegramDispatcher::peerToPublicPeer(const TLInputPeer &inputPeer) const
{
    switch (inputPeer.tlType) {
    case TLValue::InputPeerSelf:
        return Telegram::Peer(selfId());
    case TLValue::InputPeerUser:
        return Telegram::Peer(inputPeer.userId);
    case TLValue::InputPeerChat:
        return Telegram::Peer(inputPeer.chatId, Telegram::Peer::Chat);
    case TLValue::InputPeerEmpty:
    default:
        return Telegram::Peer();
    }
}

Telegram::Peer CTelegramDispatcher::peerToPublicPeer(const TLPeer &peer) const
{
    switch (peer.tlType) {
    case TLValue::PeerChat:
        return Telegram::Peer(peer.chatId, Telegram::Peer::Chat);
    case TLValue::PeerUser:
        return Telegram::Peer(peer.userId);
    default:
        return Telegram::Peer();
    }
}

TLInputUser CTelegramDispatcher::userIdToInputUser(quint32 id) const
{
    TLInputUser inputUser;

    if (id == selfId()) {
        inputUser.tlType = TLValue::InputUserSelf;
        return inputUser;
    }

    const TLUser *user = m_users.value(id);

    if (user) {
        if (user->tlType == TLValue::User) {
            inputUser.tlType = TLValue::InputUser;
            inputUser.userId = user->id;
        } else {
            qDebug() << Q_FUNC_INFO << "Unknown user type: " << QString::number(user->tlType, 16);
        }
    } else {
        qDebug() << Q_FUNC_INFO << "Unknown user.";
    }

    return inputUser;
}

QString CTelegramDispatcher::userAvatarToken(const TLUser *user) const
{
    const TLFileLocation &avatar = user->photo.photoSmall;

    if (avatar.tlType == TLValue::FileLocationUnavailable) {
        return QString();
    } else {
        return QString(QLatin1String("%1%2%3"))
                .arg(avatar.dcId, sizeof(avatar.dcId) * 2, 16, QLatin1Char('0'))
                .arg(avatar.volumeId, sizeof(avatar.dcId) * 2, 16, QLatin1Char('0'))
                .arg(avatar.localId, sizeof(avatar.dcId) * 2, 16, QLatin1Char('0'));
    }
}

CTelegramConnection *CTelegramDispatcher::getExtraConnection(quint32 dc)
{
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << dc;
#endif
    for (int i = 0; i < m_extraConnections.count(); ++i) {
        if (m_extraConnections.at(i)->dcInfo().id == dc) {
            return m_extraConnections.at(i);
        }
    }

    const TLDcOption dcInfo = dcInfoById(dc);

    if (dcInfo.ipAddress.isEmpty()) {
        qDebug() << "Error: Attempt to connect to unknown DC" << dc;
        return 0;
    }

    CTelegramConnection *connection = createConnection(dcInfo);
    if (activeConnection()->dcInfo().id == dc) {
        connection->setDeltaTime(activeConnection()->deltaTime());
        connection->setAuthKey(activeConnection()->authKey());
        connection->setServerSalt(activeConnection()->serverSalt());
    }

    m_extraConnections.append(connection);
    return connection;
}

void CTelegramDispatcher::onConnectionAuthChanged(int newState, quint32 dc)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
    qDebug() << "TelegramDispatcher::onConnectionAuthChanged():"
             << "auth" << CTelegramConnection::AuthState(newState)
             << "dc" << dc;
#else
    qDebug() << Q_FUNC_INFO << "auth" << newState << "dc" << dc;
#endif

    CTelegramConnection *connection = qobject_cast<CTelegramConnection*>(sender());

    if (!connection) {
        qDebug() << Q_FUNC_INFO << "Invalid slot call";
        return;
    }

    if (connection == activeConnection()) {
        if (newState == CTelegramConnection::AuthStateSignedIn) {
            connect(connection, SIGNAL(contactListReceived(QVector<quint32>)),
                    SLOT(whenContactListReceived(QVector<quint32>)));
            connect(connection, SIGNAL(contactListChanged(QVector<quint32>,QVector<quint32>)),
                    SLOT(whenContactListChanged(QVector<quint32>,QVector<quint32>)));
            connect(connection, SIGNAL(updatesReceived(TLUpdates,quint64)),
                    SLOT(onUpdatesReceived(TLUpdates,quint64)));
            connect(connection, SIGNAL(messagesHistoryReceived(TLMessagesMessages,TLInputPeer)),
                    SLOT(whenMessagesHistoryReceived(TLMessagesMessages)));
            connect(connection, SIGNAL(messagesDialogsReceived(TLMessagesDialogs,quint32,quint32,quint32)),
                    SLOT(onMessagesDialogsReceived(TLMessagesDialogs,quint32,quint32,quint32)));
            connect(connection, SIGNAL(updatesStateReceived(TLUpdatesState)),
                    SLOT(whenUpdatesStateReceived(TLUpdatesState)));
            connect(connection, SIGNAL(updatesDifferenceReceived(TLUpdatesDifference)),
                    SLOT(whenUpdatesDifferenceReceived(TLUpdatesDifference)));
            connect(connection, SIGNAL(authExportedAuthorizationReceived(quint32,quint32,QByteArray)),
                    SLOT(whenAuthExportedAuthorizationReceived(quint32,quint32,QByteArray)));
            connect(connection, SIGNAL(messagesChatsReceived(QVector<TLChat>)),
                    SLOT(onChatsReceived(QVector<TLChat>)));
            connect(connection, SIGNAL(messagesFullChatReceived(TLChatFull,QVector<TLChat>,QVector<TLUser>)),
                    SLOT(whenMessagesFullChatReceived(TLChatFull,QVector<TLChat>,QVector<TLUser>)));
            connect(connection, SIGNAL(userNameStatusUpdated(QString,TelegramNamespace::UserNameStatus)),
                    SIGNAL(userNameStatusUpdated(QString,TelegramNamespace::UserNameStatus)));

            continueInitialization(StepSignIn);
        } else if (newState == CTelegramConnection::AuthStateHaveAKey) {
            continueInitialization(StepFirst); // Start initialization, if it is not started yet.
        }
    } else {
        if (newState == CTelegramConnection::AuthStateSignedIn) {
            foreach (quint32 fileId, m_requestedFileDescriptors.keys()) {
                if (m_requestedFileDescriptors.value(fileId).dcId() != dc) {
                    continue;
                }

                processFileRequestForConnection(connection, fileId);
            }
        } else if (newState == CTelegramConnection::AuthStateHaveAKey) {
            ensureSignedConnection(connection);
        }
    }

    if (newState >= CTelegramConnection::AuthStateHaveAKey) {
        if (m_delayedPackages.contains(dc)) {
            qDebug() << Q_FUNC_INFO << "process" << m_delayedPackages.count(dc) << "redirected packages" << "for dc" << dc;
            foreach (const QByteArray &data, m_delayedPackages.values(dc)) {
                connection->processRedirectedPackage(data);
            }
            m_delayedPackages.remove(dc);
        }
    }

    for (CTelegramModule *module : m_modules) {
        module->onConnectionAuthChanged(connection, newState);
    }
}

void CTelegramDispatcher::onConnectionStatusChanged(int newStatus, int reason, quint32 dc)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
    qDebug() << "TelegramDispatcher::onConnectionStatusChanged():"
             << "status" << CTelegramConnection::ConnectionStatus(newStatus)
             << "reason" << CTelegramConnection::ConnectionStatusReason(reason)
             << "dc" << dc;
#else
    qDebug() << Q_FUNC_INFO << "status" << newStatus << "reason" << reason << "dc" << dc;
#endif
    CTelegramConnection *connection = qobject_cast<CTelegramConnection*>(sender());

    if (!connection) {
        qDebug() << Q_FUNC_INFO << "Invalid slot call";
        return;
    }

    if (connection == activeConnection()) {
        if (newStatus == CTelegramConnection::ConnectionStatusDisconnected) {
            if (connectionState() == TelegramNamespace::ConnectionStateDisconnected) {
                return;
            }

            if (connectionState() == TelegramNamespace::ConnectionStateConnecting) {
                // There is a problem with initial connection
                if (m_autoConnectionDcIndex >= 0) {
                    tryNextDcAddress();
                } else if (m_autoReconnectionEnabled) {
                    // Network error; try to reconnect after a second.
                    QTimer::singleShot(1000, connection, SLOT(connectToDc()));
                }
            } else {
                setConnectionState(TelegramNamespace::ConnectionStateDisconnected);

                if (m_autoReconnectionEnabled) {
                    connection->connectToDc();
                }
            }
        } else if (newStatus >= CTelegramConnection::ConnectionStatusConnected) {
            m_autoConnectionDcIndex = s_autoConnectionIndexInvalid;
        }
    }
}

void CTelegramDispatcher::onDcConfigurationUpdated()
{
    CTelegramConnection *connection = qobject_cast<CTelegramConnection*>(sender());

    if (!connection) {
        return;
    }

    if (connection != m_mainConnection) {
        qDebug() << "Got configuration from extra connection. Ignored.";
        return;
    }

    m_dcConfiguration = connection->dcConfiguration();

    qDebug() << "Core: Got DC Configuration.";

    foreach (TLDcOption o, m_dcConfiguration) {
        qDebug() << o.id << o.ipAddress << o.port;
    }

    continueInitialization(StepDcConfiguration);

    ensureMainConnectToWantedDc();
}

void CTelegramDispatcher::onConnectionDcIdUpdated(quint32 connectionId, quint32 newDcId)
{
    CTelegramConnection *connection = qobject_cast<CTelegramConnection*>(sender());

    if (!connection) {
        return;
    }

    qDebug() << "Connection" << connection << "DC Id changed from" << connectionId << "to" << newDcId;
    if (connection == m_mainConnection) {
        if (m_wantedActiveDc && (m_wantedActiveDc != m_mainConnection->dcInfo().id)) {
            qDebug() << Q_FUNC_INFO << "Wanted active dc is different from the actual main connection dc. Do we need to do anything?";
        }
    }
}

void CTelegramDispatcher::onPackageRedirected(const QByteArray &data, quint32 dc)
{
    CTelegramConnection *connection = getExtraConnection(dc);

    if (connection->authState() >= CTelegramConnection::AuthStateHaveAKey) {
        connection->processRedirectedPackage(data);
    } else {
        m_delayedPackages.insertMulti(dc, data);

        if (connection->status() == CTelegramConnection::ConnectionStatusDisconnected) {
            connection->connectToDc();
        }
    }
}

void CTelegramDispatcher::whenFileDataReceived(const TLUploadFile &file, quint32 requestId, quint32 offset)
{
    if (!m_requestedFileDescriptors.contains(requestId)) {
        qDebug() << Q_FUNC_INFO << "Unexpected request" << requestId;
        return;
    }

    FileRequestDescriptor &descriptor = m_requestedFileDescriptors[requestId];
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << "File:" << file.tlType << file.type << "mtime:" << file.mtime;
    qDebug() << Q_FUNC_INFO
             << "Descriptor:" << "request:" << requestId
             << "type:" << descriptor.type()
             << "size:" << descriptor.size()
             << "ids:" << descriptor.userId() << descriptor.messageId();
#endif

    if (descriptor.type() != FileRequestDescriptor::Download) {
        return;
    }

    const quint32 chunkSize = file.bytes.size();

    QString mimeType = mimeTypeByStorageFileType(file.type.tlType);

    // Depends on InputFileLocation tlType, we can either have descriptor.size() (for MediaMessage data (Audio, Video, Document)),
    // or have file type StorageFilePartial otherwise.

    bool isFinished = false;

    if (descriptor.size()) {
        isFinished = descriptor.offset() + chunkSize == descriptor.size();
    } else {
        isFinished = file.type.tlType != TLValue::StorageFilePartial;
    }

    if (isFinished) {
        descriptor.setSize(descriptor.offset() + chunkSize);
    }

    emit filePartReceived(requestId, file.bytes, mimeType, descriptor.offset(), descriptor.size()); // Size can be unknown (== 0)

    if (isFinished) {
#ifdef DEVELOPER_BUILD
        qDebug() << Q_FUNC_INFO << "file" << requestId << "download finished.";
#endif

        Telegram::RemoteFile result;
        const TLInputFileLocation location = descriptor.inputLocation();
        result.d->setInputFileLocation(&location);
        result.d->m_dcId = descriptor.dcId();
        result.d->m_type = Telegram::RemoteFile::Download;
        emit fileRequestFinished(requestId, result);

        m_requestedFileDescriptors.remove(requestId);
    } else {
        descriptor.setOffset(offset + chunkSize);

        CTelegramConnection *connection = qobject_cast<CTelegramConnection*>(sender());
        if (connection) {
            processFileRequestForConnection(connection, requestId);
        } else {
            qDebug() << Q_FUNC_INFO << "Invalid call. The method must be called only on CTelegramConnection signal.";
        }
    }

    // Legacy stuff:
    if (isFinished) {
        if (descriptor.userId() && m_users.contains(descriptor.userId())) {
            emit avatarReceived(descriptor.userId(), file.bytes, mimeType, userAvatarToken(m_users.value(descriptor.userId())));
        }
    }

    const TLMessage *message = nullptr;
    if (descriptor.messageId() && (message = getMessage(descriptor.messageId()))) {
        const TelegramNamespace::MessageType messageType = telegramMessageTypeToPublicMessageType(message->media.tlType);

        Telegram::Peer peer = peerToPublicPeer(message->toId);

        // MimeType can not be resolved for some StorageFileType. Try to get the type from the message info in this case.
        if (mimeType.isEmpty()) {
            Telegram::MessageMediaInfo info;
            getMessageMediaInfo(&info, message->id);
            mimeType = info.mimeType();
        }

        if (!(message->flags & TelegramMessageFlagOut)) {
            if (peer.type == Telegram::Peer::User) {
                peer = message->fromId;
            }
        }

#ifdef DEVELOPER_BUILD
        qDebug() << Q_FUNC_INFO << "MessageMediaData:" << message->id << offset << "-" << offset + chunkSize << "/" << descriptor.size();
#endif
        emit messageMediaDataReceived(peer, message->id, file.bytes, mimeType, messageType, offset, descriptor.size());
    }
}

void CTelegramDispatcher::whenFileDataUploaded(quint32 requestId)
{
    if (!m_requestedFileDescriptors.contains(requestId)) {
        qDebug() << Q_FUNC_INFO << "Unexpected fileId" << requestId;
        return;
    }

    FileRequestDescriptor &descriptor = m_requestedFileDescriptors[requestId];

    if (descriptor.type() != FileRequestDescriptor::Upload) {
        return;
    }

    descriptor.bumpPart();

    emit filePartUploaded(requestId, descriptor.offset(), descriptor.size());

    if (descriptor.finished()) {
        Telegram::RemoteFile result;
        const TLInputFile fileInfo = descriptor.inputFile();
        result.d->m_size = descriptor.size();
        result.d->m_type = Telegram::RemoteFile::Upload;
        result.d->setInputFile(&fileInfo);

        emit fileRequestFinished(requestId, result);
        return;
    }

    CTelegramConnection *connection = qobject_cast<CTelegramConnection*>(sender());
    if (connection) {
        processFileRequestForConnection(connection, requestId);
    } else {
        qDebug() << Q_FUNC_INFO << "Invalid call. The method must be called only on CTelegramConnection signal.";
    }
}

void CTelegramDispatcher::onUpdatesReceived(const TLUpdates &updates, quint64 id)
{
#ifdef DEVELOPER_BUILD
    qDebug() << Q_FUNC_INFO << updates << id;
#else
    qDebug() << Q_FUNC_INFO;
#endif
    m_updateRequestId = id;

    switch (updates.tlType) {
    case TLValue::UpdatesTooLong:
        qDebug() << "Updates too long!";
        getUpdatesState();
        break;
    case TLValue::UpdateShortMessage:
    case TLValue::UpdateShortChatMessage:
    {
        // Reconstruct full update from this short update.
        TLUpdate update;
        update.tlType = TLValue::UpdateNewMessage;
        update.pts = updates.pts;
        update.ptsCount = updates.ptsCount;
        TLMessage &shortMessage = update.message;
        shortMessage.tlType = TLValue::Message;
        shortMessage.id = updates.id;
        shortMessage.flags = updates.flags;
        shortMessage.message = updates.message;
        shortMessage.date = updates.date;
        shortMessage.media.tlType = TLValue::MessageMediaEmpty;
        shortMessage.fwdFromId = updates.fwdFromId;
        shortMessage.fwdDate = updates.fwdDate;
        shortMessage.replyToMsgId = updates.replyToMsgId;

        int messageActionIndex = 0;
        if (updates.tlType == TLValue::UpdateShortMessage) {
            shortMessage.toId.tlType = TLValue::PeerUser;

            if (shortMessage.flags & TelegramMessageFlagOut) {
                shortMessage.toId.userId = updates.userId;
                shortMessage.fromId = selfId();
            } else {
                shortMessage.toId.userId = selfId();
                shortMessage.fromId = updates.userId;
            }

            messageActionIndex = TypingStatus::indexForUser(m_contactsMessageActions, updates.fromId);
            if (messageActionIndex >= 0) {
                emit contactMessageActionChanged(updates.fromId, TelegramNamespace::MessageActionNone);
            }

        } else {
            shortMessage.toId.tlType = TLValue::PeerChat;
            shortMessage.toId.chatId = updates.chatId;

            shortMessage.fromId = updates.fromId;

            messageActionIndex = TypingStatus::indexForUser(m_contactsMessageActions, updates.fromId);
            if (messageActionIndex >= 0) {
                emit contactChatMessageActionChanged(updates.chatId,
                                                    updates.fromId,
                                                    TelegramNamespace::MessageActionNone);
            }
        }

        processUpdate(update);

        if (messageActionIndex > 0) {
            m_contactsMessageActions.remove(messageActionIndex);
        }
    }
        break;
    case TLValue::UpdateShort:
        processUpdate(updates.update);
        break;
    case TLValue::UpdatesCombined:
        qDebug() << Q_FUNC_INFO << "UpdatesCombined processing is not implemented yet.";
        Q_ASSERT(0);
        break;
    case TLValue::Updates:
        onUsersReceived(updates.users);
        onChatsReceived(updates.chats);

        // TODO: ensureUpdateState(, updates.seq, updates.date);?

        if (!updates.updates.isEmpty()) {
            // Official client sorts updates by pts/qts. Wat?!
            // Ok, let's see if there would be unordered updates.
            quint32 pts = updates.updates.first().pts;
            for (int i = 0; i < updates.updates.count(); ++i) {
                if (updates.updates.at(i).pts < pts) {
                    qDebug() << "Unordered update!";
                    Q_ASSERT(0);
                }
                pts = updates.updates.at(i).pts;
            }

            // Initial implementation
            for (int i = 0; i < updates.updates.count(); ++i) {
                processUpdate(updates.updates.at(i));
            }
        }
        break;
    case TLValue::UpdateShortSentMessage:
        if (!m_rpcIdToMessageRandomIdMap.contains(id)) {
            qDebug() << Q_FUNC_INFO << "Sent message id not found";
        } else {
            const quint64 randomId = m_rpcIdToMessageRandomIdMap.take(id);
            updateSentMessageId(randomId, updates.id);
        }
        // TODO: Check that the follow state update is the right thing to do.
        // This fixes scenario: "send sendMessage" -> "receive UpdateShortSentMessage" -> "receive UpdateReadHistoryOutbox with update.pts == m_updatesState.pts + 2"
        setUpdateState(m_updatesState.pts + 1, 0, 0);
        break;
    default:
        break;
    }

    m_updateRequestId = 0;
}

void CTelegramDispatcher::whenAuthExportedAuthorizationReceived(quint32 dc, quint32 id, const QByteArray &data)
{
    m_exportedAuthentications.insert(dc, QPair<quint32, QByteArray>(id,data));

    CTelegramConnection *connection = 0;

    for (int i = 0; i < m_extraConnections.count(); ++i) {
        if (m_extraConnections.at(i)->dcInfo().id == dc) {
            connection = m_extraConnections.at(i);
            break;
        }
    }

    if (connection && (connection->authState() == CTelegramConnection::AuthStateHaveAKey)) {
        connection->authImportAuthorization(id, data);
    }
}

void CTelegramDispatcher::ensureTypingUpdateTimer(int interval)
{
    if (!m_typingUpdateTimer->isActive()) {
        m_typingUpdateTimer->start(interval);
    }
}

void CTelegramDispatcher::continueInitialization(CTelegramDispatcher::InitializationStep justDone)
{
    qDebug() << Q_FUNC_INFO << justDone;

    if (justDone && ((m_initializationState | justDone) == m_initializationState)) {
        return; // Nothing new
    }

    m_initializationState = InitializationStep(m_initializationState|justDone);

    if (!(m_requestedSteps & StepDcConfiguration)) { // DC configuration is not requested yet
        getDcConfiguration();
        m_requestedSteps |= StepDcConfiguration;
    }

    if (!(m_initializationState & StepDcConfiguration)) { // DC configuration is unknown yet
        return;
    }

    if (justDone == StepDcConfiguration) {
        if (activeConnection()->authState() == CTelegramConnection::AuthStateHaveAKey) {
            setConnectionState(TelegramNamespace::ConnectionStateAuthRequired);
        } else {
            setConnectionState(TelegramNamespace::ConnectionStateConnected);
        }
    }

    if ((m_initializationState & StepDcConfiguration) && (m_initializationState & StepSignIn)) {
        setConnectionState(TelegramNamespace::ConnectionStateAuthenticated);
        m_deltaTime = activeConnection()->deltaTime();

        if (!(m_requestedSteps & StepKnowSelf)) {
            getInitialUsers();
            m_requestedSteps |= StepKnowSelf;
            return;
        }

        if (!(m_requestedSteps & StepContactList)) {
            getContacts();
            m_requestedSteps |= StepContactList;
        }

        if (!(m_requestedSteps & StepChatInfo)) {
            getChatsInfo();
            m_requestedSteps |= StepChatInfo;
        }

        if (!(m_requestedSteps & StepDialogs)) {
            getInitialDialogs();
            m_requestedSteps |= StepDialogs;
        }
    }

    if (!m_updatesEnabled) {
        if ((m_initializationState | StepUpdates) == StepDone) {
            setConnectionState(TelegramNamespace::ConnectionStateReady);
        }
        return;
    }

    if (m_initializationState == StepDone) {
        setConnectionState(TelegramNamespace::ConnectionStateReady);
        return;
    }

    if (m_initializationState & StepContactList) {
        if (!(m_requestedSteps & StepUpdates)) {
            getUpdatesState();
            m_requestedSteps |= StepUpdates;
        }
    }
}

// Basically we just revert Unread and Read flag.
TelegramNamespace::MessageFlags CTelegramDispatcher::getPublicMessageFlags(quint32 flags)
{
    TelegramNamespace::MessageFlags result = TelegramNamespace::MessageFlagNone;

    if (!(flags & TelegramMessageFlagUnread)) {
        result |= TelegramNamespace::MessageFlagRead;
    }

    if (flags & TelegramMessageFlagOut) {
        result |= TelegramNamespace::MessageFlagOut;
    }

    if (flags & TelegramMessageFlagForward) {
        result |= TelegramNamespace::MessageFlagForwarded;
    }

    if (flags & TelegramMessageFlagReply) {
        result |= TelegramNamespace::MessageFlagIsReply;
    }

    return result;
}

void CTelegramDispatcher::ensureUpdateState(quint32 pts, quint32 seq, quint32 date)
{
    if (m_updatesStateIsLocked) {
        qDebug() << Q_FUNC_INFO << pts << seq << date << "locked.";
        /* Prevent m_updateState from updating before UpdatesGetState answer receiving to avoid
         * m_updateState <-> m_actualState messing (which may lead to ignore offline-messages) */
        return;
    }

    setUpdateState(pts, seq, date);
}

void CTelegramDispatcher::setUpdateState(quint32 pts, quint32 seq, quint32 date)
{
    qDebug() << Q_FUNC_INFO << pts << seq << date;

    if (pts > m_updatesState.pts) {
        qDebug() << Q_FUNC_INFO << "Update pts from " << m_updatesState.pts << "to" << pts;
        m_updatesState.pts = pts;
    }

    if (seq > m_updatesState.seq) {
        m_updatesState.seq = seq;
    }

    if (date > m_updatesState.date) {
        qDebug() << Q_FUNC_INFO << "Update date from " << m_updatesState.date << "to" << date;
        m_updatesState.date = date;
    }
}

void CTelegramDispatcher::ensureMaxMessageId(quint32 id)
{
    if (m_maxMessageId < id) {
        m_maxMessageId = id;
    }
}

void CTelegramDispatcher::updateSentMessageId(quint64 randomId, quint32 resolvedId)
{
    qDebug() << Q_FUNC_INFO << "Sent message id received:" << resolvedId << "is the id of message" << randomId;
    ensureMaxMessageId(resolvedId);
    emit sentMessageIdReceived(randomId, resolvedId);
}

void CTelegramDispatcher::checkStateAndCallGetDifference()
{
    m_updatesStateIsLocked = m_actualState.pts > m_updatesState.pts;

    if (m_updatesStateIsLocked) {
        QTimer::singleShot(10, this, SLOT(getDifference()));
    } else {
        continueInitialization(StepUpdates);
    }
}

CTelegramConnection *CTelegramDispatcher::createConnection(const TLDcOption &dcInfo)
{
    qDebug() << Q_FUNC_INFO << dcInfo.id << dcInfo.ipAddress << dcInfo.port;

    CTelegramConnection *connection = new CTelegramConnection(m_appInformation, this);
    connection->setDcInfo(dcInfo);
    connection->setDeltaTime(m_deltaTime);

    connect(connection, SIGNAL(authStateChanged(int,quint32)), SLOT(onConnectionAuthChanged(int,quint32)));
    connect(connection, SIGNAL(statusChanged(int,int,quint32)), SLOT(onConnectionStatusChanged(int,int,quint32)));
    connect(connection, SIGNAL(dcConfigurationReceived(quint32)), SLOT(onDcConfigurationUpdated()));
    connect(connection, SIGNAL(actualDcIdReceived(quint32,quint32)), SLOT(onConnectionDcIdUpdated(quint32,quint32)));
    connect(connection, SIGNAL(newRedirectedPackage(QByteArray,quint32)), SLOT(onPackageRedirected(QByteArray,quint32)));

    connect(connection, SIGNAL(selfUserReceived(TLUser)), SLOT(onSelfUserReceived(TLUser)));
    connect(connection, SIGNAL(usersReceived(QVector<TLUser>)),
            SLOT(onUsersReceived(QVector<TLUser>)));
    connect(connection, SIGNAL(fileDataReceived(TLUploadFile,quint32,quint32)), SLOT(whenFileDataReceived(TLUploadFile,quint32,quint32)));
    connect(connection, SIGNAL(fileDataSent(quint32)), SLOT(whenFileDataUploaded(quint32)));

    for (CTelegramModule *module : m_modules) {
        module->onNewConnection(connection);
    }

    return connection;
}

void CTelegramDispatcher::ensureSignedConnection(CTelegramConnection *connection)
{
    if (connection->status() == CTelegramConnection::ConnectionStatusDisconnected) {
        connection->connectToDc();
    } else {
        if (connection->authState() == CTelegramConnection::AuthStateHaveAKey) { // Need an exported auth to sign in
            quint32 dc = connection->dcInfo().id;

            if (dc == 0) {
                qWarning() << Q_FUNC_INFO << "Invalid dc id" << connection;
                return;
            }

            if (activeConnection()->dcInfo().id == dc) {
                connection->setDeltaTime(activeConnection()->deltaTime());
                connection->setAuthKey(activeConnection()->authKey());
                connection->setServerSalt(activeConnection()->serverSalt());
                return;
            }

            if (m_exportedAuthentications.contains(dc)) {
                connection->authImportAuthorization(m_exportedAuthentications.value(dc).first, m_exportedAuthentications.value(dc).second);
            } else {
                if (activeConnection()->authState() == CTelegramConnection::AuthStateSignedIn) {
                    activeConnection()->authExportAuthorization(dc);
                }
            }
        }
    }
}

void CTelegramDispatcher::clearMainConnection()
{
    if (!m_mainConnection) {
        return;
    }
    m_mainConnection->deleteLater();
    m_mainConnection = nullptr;
}

void CTelegramDispatcher::clearExtraConnections()
{
    foreach (CTelegramConnection *connection, m_extraConnections) {
        disconnect(connection, nullptr, this, nullptr);
        connection->deleteLater();
    }

    m_extraConnections.clear();
}

void CTelegramDispatcher::ensureMainConnectToWantedDc()
{
    if (!m_mainConnection) {
        qWarning() << Q_FUNC_INFO << "Unable to operate without connection.";
        return;
    }

    if (m_mainConnection->dcInfo().id == m_wantedActiveDc) {
        qDebug() << Q_FUNC_INFO << "Nothing to do. Wanted DC is already connected.";
        return;
    }

    TLDcOption wantedDcInfo = dcInfoById(m_wantedActiveDc);

    if (wantedDcInfo.ipAddress.isEmpty()) {
        if (m_initializationState & StepDcConfiguration) {
            qWarning() << Q_FUNC_INFO << "Unable to connect: wanted DC is not listed in received DC configuration.";
            return;
        }
        qDebug() << Q_FUNC_INFO << "Wanted dc is unknown. Requesting configuration...";
        getDcConfiguration();
        return;
    }

    clearMainConnection();
    m_mainConnection = createConnection(wantedDcInfo);
    m_mainConnection->connectToDc();
}

TLDcOption CTelegramDispatcher::dcInfoById(quint32 dc) const
{
    foreach (const TLDcOption option, m_dcConfiguration) {
        if (option.id == dc) {
            return option;
        }
    }

    return TLDcOption();
}

const TLUser *CTelegramDispatcher::getUser(quint32 userId) const
{
    return m_users.value(userId);
}

const TLMessage *CTelegramDispatcher::getMessage(quint32 messageId) const
{
    return m_knownMediaMessages.value(messageId);
}