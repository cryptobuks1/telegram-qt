#ifndef TELEGRAM_SERVER_NAMESPACE_HPP
#define TELEGRAM_SERVER_NAMESPACE_HPP

#include "TelegramNamespace.hpp"

#include <QString>

namespace Telegram {

namespace Server {

struct InputPeer : public Telegram::Peer
{
    InputPeer() = default;
    InputPeer(const Telegram::Peer &peer);

    qint64 accessHash = 0;
};

struct ServerSalt
{
    quint64 salt = 0;
    quint32 validSince = 0;
    quint32 validUntil = 0;
};

struct UserDialog
{
    Telegram::Peer peer;
    quint32 topMessage = 0;
    QString draftText;
};

struct UserContact
{
    quint32 id = 0;
    QString phone;
    QString firstName;
    QString lastName;
};

} // Server namespace

} // Telegram namespace

#endif // TELEGRAM_SERVER_NAMESPACE_HPP
