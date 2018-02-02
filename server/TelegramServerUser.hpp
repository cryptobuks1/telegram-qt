#ifndef TELEGRAMSERVERUSER_HPP
#define TELEGRAMSERVERUSER_HPP

#include <QObject>
#include <QVector>

#include "../TelegramQt/CAppInformation.hpp"

namespace Telegram {

namespace Server {

struct Session
{
    Session()
    {
    }

    Session(const Session &otherSession) :
        authId(otherSession.authId),
        appInfo(nullptr),
        timestamp(otherSession.timestamp)
    {
        if (otherSession.appInfo) {
            appInfo = new CAppInformation(otherSession.appInfo);
        }
    }

    quint64 authId = 0;
    quint64 sessionId = 0;
    QByteArray authKey;
    CAppInformation *appInfo = nullptr;
    QString ip;
    quint64 timestamp = 0;
};

class User : public QObject
{
    Q_OBJECT
public:
    explicit User(QObject *parent = nullptr);

    quint32 id() const { return m_id; }
    QString phoneNumber() const { return m_phoneNumber; }
    void setPhoneNumber(const QString &phoneNumber);

    QString firstName() const { return m_firstName; }
    void setFirstName(const QString &firstName);

    QString lastName() const { return m_lastName; }
    void setLastName(const QString &lastName);

    bool isOnline() const;

    quint32 dcId() const { return m_dcId; }
    void setDcId(quint32 id);

    Session getSession(quint64 authId) const;
    QVector<Session> sessions() const { return m_sessions; }
    void addSession(const Session &session);

    bool hasPassword() const { return !m_passwordSalt.isEmpty() && !m_passwordHash.isEmpty(); }
    QByteArray passwordSalt() const { return m_passwordSalt; }
    QByteArray passwordHash() const { return m_passwordHash; }

    void setPlainPassword(const QString &password);
    void setPassword(const QByteArray &salt, const QByteArray &hash);

    QString passwordHint() const { return QString(); }

protected:
    quint32 m_id;
    QString m_phoneNumber;
    QString m_firstName;
    QString m_lastName;
    QString m_userName;
    QByteArray m_passwordSalt;
    QByteArray m_passwordHash;
    QVector<Session> m_sessions;
    quint32 m_dcId = 0;
};

} // Server

} // Telegram

#endif // TELEGRAMSERVERUSER_HPP
