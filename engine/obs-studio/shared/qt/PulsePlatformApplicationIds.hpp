#pragma once

#include <QString>

// Platform Client IDs identify the public desktop application. They are not
// secrets, tokens or credentials. User access and refresh tokens remain
// per-user, protected in Pulse Weaver's isolated configuration. Confidential
// client secrets stay in the hosted Pulse Weaver relay.
namespace PulsePlatformApplicationIds {
inline QString TwitchClientId()
{
	return QStringLiteral("yf6dwbns8wt6q893i0pdfdppy0m7ka");
}

inline QString KickClientId()
{
	return QStringLiteral("01M1VYM6YB64Y8KP09GV07PYB3");
}
}
