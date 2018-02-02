#include "TelegramServer.hpp"

#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>

#include "Utils.hpp"
#include "TelegramServerUser.hpp"
#include "TelegramServerClient.hpp"

#include "CServerTcpTransport.hpp"

// Generated RPC Operation Factory includes
#include "AccountOperationFactory.hpp"
#include "AuthOperationFactory.hpp"
#include "ChannelsOperationFactory.hpp"
#include "ContactsOperationFactory.hpp"
#include "HelpOperationFactory.hpp"
#include "MessagesOperationFactory.hpp"
#include "PhotosOperationFactory.hpp"
#include "UpdatesOperationFactory.hpp"
#include "UploadOperationFactory.hpp"
#include "UsersOperationFactory.hpp"
// End of generated RPC Operation Factory includes

Q_LOGGING_CATEGORY(loggingCategoryServer, "telegram.server.main", QtDebugMsg)
Q_LOGGING_CATEGORY(loggingCategoryServerApi, "telegram.server.api", QtDebugMsg)

namespace Telegram {

namespace Server {

Server::Server(QObject *parent) :
    QObject(parent)
{
    m_rpcOperationFactories = {
        // Generated RPC Operation Factory initialization
        new AccountOperationFactory(),
        new AuthOperationFactory(),
        new ChannelsOperationFactory(),
        new ContactsOperationFactory(),
        new HelpOperationFactory(),
        new MessagesOperationFactory(),
        new PhotosOperationFactory(),
        new UpdatesOperationFactory(),
        new UploadOperationFactory(),
        new UsersOperationFactory(),
        // End of generated RPC Operation Factory initialization
    };
    m_serverSocket = new QTcpServer(this);
    connect(m_serverSocket, &QTcpServer::newConnection, this, &Server::onNewConnection);
}

void Server::setDcOption(const TLDcOption &option)
{
    m_dcOption = option;
}

void Server::setServerPrivateRsaKey(const Telegram::RsaKey &key)
{
    m_key = key;
}

bool Server::start()
{
    if (!m_serverSocket->listen(QHostAddress::Any, m_dcOption.port)) {
        qWarning() << "Unable to listen port" << m_dcOption.port;
        return false;
    }
    qDebug() << "Start a server" << m_dcOption.id << "on port" << m_dcOption.port << "Key:" << m_key.fingerprint;
    return true;
}

void Server::loadData()
{
    const int number = 10;
    for (int i = 0; i < number; ++i) {
        User *newUser = new User(this);
        newUser->setPhoneNumber(QStringLiteral("%1").arg(i, 6, 10, QLatin1Char('0')));
        insertUser(newUser);
    }
}

quint32 Server::getDcIdForUserIdentifier(const QString &phoneNumber)
{
    if (m_phoneToUserId.contains(phoneNumber)) {
        return m_dcOption.id;
    }
    return 0;
}

void Server::onNewConnection()
{
    QTcpSocket *newConnection = m_serverSocket->nextPendingConnection();
    if (newConnection == nullptr) {
        qCDebug(loggingCategoryServer) << "expected pending connection does not exist";
        return;
    }
    qCDebug(loggingCategoryServer) << "A new incoming connection from" << newConnection->peerAddress().toString();
    TcpTransport *transport = new TcpTransport(newConnection, this);
    newConnection->setParent(transport);
    RemoteClientConnection *client = new RemoteClientConnection(this);
    connect(client, &BaseConnection::statusChanged, this, &Server::onClientConnectionStatusChanged);
    client->setServerRsaKey(m_key);
    client->setTransport(transport);
    client->setServerApi(this);
    client->setRpcFactories(m_rpcOperationFactories);

    m_activeConnections.insert(client);
}

void Server::onClientConnectionStatusChanged()
{
    RemoteClientConnection *client = qobject_cast<RemoteClientConnection*>(sender());
    if (client->status() == RemoteClientConnection::Status::Authenticated) {
        User *u = getUser(client->authId());
        if (u) {
            client->setUser(u);
        } else {
            qDebug() << Q_FUNC_INFO << "A new auth key";
        }
    }
}

User *Server::getUser(const QString &identifier)
{
    quint32 id = m_phoneToUserId.value(identifier);
    if (!id) {
        return nullptr;
    }
    return m_users.value(id);
}

User *Server::getUser(quint64 authId)
{
    quint32 id = m_authIdToUserId.value(authId);
    if (!id) {
        return nullptr;
    }
    return m_users.value(id);
}

User *Server::addUser(const QString &identifier)
{
    qDebug() << Q_FUNC_INFO << identifier;
    User *user = new User(this);
    user->setPhoneNumber(identifier);
    user->setDcId(dcId());
    insertUser(user);
    return user;
}

void Server::insertUser(User *user)
{
    qDebug() << Q_FUNC_INFO << user << user->phoneNumber() << user->id();
    m_users.insert(user->id(), user);
    m_phoneToUserId.insert(user->phoneNumber(), user->id());
    for (const Session &session : user->sessions()) {
        m_authIdToUserId.insert(session.authId, user->id());
    }
}

PhoneStatus Server::getPhoneStatus(const QString &identifier)
{
    PhoneStatus result;
    User *user = getUser(identifier);
    if (user) {
        result.online = user->isOnline();
        result.dcId = user->dcId();
    }
    return result;
}

PasswordInfo Server::getPassword(const QString &identifier)
{
    PasswordInfo result;
    User *user = getUser(identifier);
    if (user && user->hasPassword()) {
        result.currentSalt = user->passwordSalt();
        result.hint = user->passwordHint();
    }
    return result;
}

bool Server::checkPassword(const QString &identifier, const QString &hash)
{
    User *user = getUser(identifier);
    if (user && user->hasPassword()) {
        return user->passwordHash() == hash;
    }
    return false;

}

QString Server::sendAppCode(const QString &identifier)
{
    AuthCode code;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    QRandomGenerator;
#endif

    QByteArray randBytes(8, Qt::Uninitialized);
    Utils::randomBytes(&randBytes);
    code.hash = QString::fromLatin1(randBytes.toHex());
    code.code = QString::number(Utils::randomBytes<quint32>()).right(5);

    qCDebug(loggingCategoryServerApi) << "sendAppCode(" << identifier << "):" << "hash:" << code.hash << "code:" << code.code;
    m_sentCodeMap.insert(identifier, code);
    return code.hash;
}

ServerApi::AuthCodeStatus Server::getAuthCodeStatus(const QString &identifier, const QString &hash, const QString &code)
{
    if (code.isEmpty()) {
        return AuthCodeStatus::CodeEmpty;
    }
    if (hash.isEmpty()) {
        return AuthCodeStatus::HashEmpty;
    }
    if (!m_sentCodeMap.contains(identifier)) {
        return AuthCodeStatus::PhoneInvalid;
    }
    const AuthCode c = m_sentCodeMap.value(identifier);
    if (c.hash != hash) {
        return AuthCodeStatus::HashInvalid;
    }
    if (c.code != code) {
        return AuthCodeStatus::CodeInvalid;
    }
    return AuthCodeStatus::CodeValid;
}

bool Server::identifierIsValid(const QString &identifier)
{
    const bool result = identifier.length() > 4;
    qCDebug(loggingCategoryServerApi) << "identifierIsValid(" << identifier << "):" << result;
    return result;
}

} // Server

} // Telegram
