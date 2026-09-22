# Pulse Weaver Studio Privacy Policy

**Effective: 22 September 2026**

Pulse Weaver is local-first streaming software. This notice explains what stays on your computer, what connected services receive, and the limited information handled by the hosted Kick relay.

## 1. Who is responsible

Pulse Weaver Studio is provided by **Daryl Wickham**. For privacy questions or requests, email **darylbwickham@gmail.com**. Do not email passwords, stream keys, access tokens or unredacted configuration backups.

## 2. What the desktop application handles

Pulse Weaver processes the information needed for features you choose to use. This can include scenes, sources, transforms, local file paths, recordings, output settings, channel details, chat messages, viewer events and connected-account credentials.

Application settings, credentials, browser data and recordings are normally stored on your computer. Sensitive connected-account credentials are protected for the current Windows account where the application supports that protection. Pulse Weaver does not operate advertising, behavioural analytics or a user-profile database, and we do not sell personal information.

Installer backups can contain application configuration and encrypted credentials. They remain on your computer in the Pulse Weaver Backups folder and are not uploaded to us. Protect them, remove copies you no longer need and do not restore a credential after revoking it.

## 3. Connected services and user-selected destinations

When you connect or configure a service, Pulse Weaver sends the information needed for that feature to the service you selected. Streaming platforms receive your broadcasts and account/API requests. Browser sources, docks, plugins, webhooks, lighting services and optional AI tools can receive information under their own terms and privacy notices.

GitHub hosts the project repository, releases and update information. A request to GitHub or another internet service necessarily provides network and device information to that service. Public GitHub issues are visible to other people. Review logs, screenshots and exports before sharing them.

## 4. YouTube and Google API data

Pulse Weaver uses YouTube API Services. If you connect YouTube, the application may access your channel identity, live broadcasts and streams, live-chat identifiers and messages, participants, memberships and public subscriber events. It uses that information to create and manage broadcasts, transmit your selected output, display and post live chat, perform moderation you request, and drive your local alerts and automations.

Google OAuth access and refresh tokens and the connected channel name are stored locally so the application can remain connected. Live-chat and audience-event data is held in bounded working memory while needed for the live session. Pulse Weaver does not use Google user data for advertising, sale, credit decisions or general-purpose AI training, and does not combine data from unrelated channel owners.

### Who receives Google user data

Pulse Weaver shares, transfers or discloses Google user data only in these limited circumstances:

- **Google and YouTube:** the desktop application sends OAuth tokens and the API requests needed to carry out actions you choose, such as creating or ending a broadcast, reading or posting live chat, or applying moderation. Broadcasts, chat messages and other content you choose to publish are then visible according to your YouTube settings.
- **Integrations you deliberately configure:** a browser source, plugin, webhook, lighting service or automation tool can receive selected YouTube event or chat information only when you configure that integration to use it. That provider handles the information under its own privacy notice. Google OAuth tokens are not intentionally sent to these integrations.
- **Support or legal disclosure:** Daryl Wickham receives Google user data only if you deliberately include it in a support request, or if disclosure is required by law or is necessary to protect users, the service or legal rights. You are not required to provide Google user data for support and should never send access tokens or refresh tokens.

Pulse Weaver does **not automatically transfer Google user data** to Daryl Wickham, the hosted Kick relay, OpenAI/ChatGPT Sites, Cloudflare, GitHub, advertisers, data brokers, information resellers, lenders, or general-purpose AI or machine-learning systems. We do not sell Google user data or use it for advertising, retargeting, credit decisions or lending. The desktop application exchanges Google credentials directly with Google's OAuth and YouTube API endpoints; the credentials remain on the user's computer apart from those exchanges.

Google's handling of information is described in the [Google Privacy Policy](https://policies.google.com/privacy). You may disconnect inside Pulse Weaver or revoke access from [Google's permissions page](https://security.google.com/settings/security/permissions). Pulse Weaver attempts to revoke the token and immediately removes its live local credentials and transient YouTube session data. Revocation does not delete content held by YouTube, user-created recordings/exports or user-controlled installer backups.

## 5. Hosted Kick relay

The [Pulse Weaver Kick Relay](https://pulse-weaver-kick-relay.darylbwickham.chatgpt.site) is hosted through ChatGPT Sites on Cloudflare Workers and Cloudflare D1. It exchanges Kick authorisation codes or refresh tokens with Kick and validates a Kick access token when establishing a relay session. The service code does not write those OAuth credentials to its database.

The relay database can hold a broadcaster ID, a hashed short-lived relay-session token, Kick event identifiers and an event payload such as a chat message or viewer event. Events are accepted only while that broadcaster has an active desktop relay session. They are no longer returned after 15 minutes and are deleted when delivered or during subsequent relay activity. Session hashes expire after one hour. Deleted D1 records may remain in Cloudflare recovery history for up to 30 days, depending on the hosting plan.

Operational logs can include the request URL and method, IP address, approximate country, user agent, response status and request identifiers. Authorisation and cookie header values are redacted in the log view available to us. Recent logs are used for security and fault diagnosis and may be available to us for up to seven days; infrastructure providers may keep records as described in their own terms.

## 6. Why we process information

Where UK data-protection law applies, we process information as necessary to provide features you request and perform our agreement with you; for legitimate interests in securing, diagnosing and supporting Pulse Weaver; and where needed to comply with legal obligations. We ask for consent where applicable law requires it. Platform authorisation is permission to access that platform and is not treated as a universal legal basis for every other use.

## 7. Service providers, transfers and security

Relevant recipients can include OpenAI/ChatGPT Sites and Cloudflare for relay hosting, GitHub for source and releases, and the platforms or destinations you choose. These organisations may process information outside the United Kingdom under their own terms and applicable transfer safeguards.

We use data minimisation, short-lived relay sessions, hashed relay tokens, transport encryption and restricted access. No system is completely secure. Keep Pulse Weaver, Windows and your plugins updated, limit account permissions, and never publish your configuration or backup folders.

## 8. Streamers and other people's information

If you decide to display, record or share information about viewers, guests, employees or other people, you may be a separate data controller. You are responsible for a lawful basis, appropriate notices, security, retention and handling people's rights. This includes chat overlays, alerts, recordings and third-party integrations. Your responsibility does not remove ours for a service we operate.

## 9. Your rights and complaints

Depending on the circumstances, you may have rights of access, correction, deletion, restriction, objection and portability, and the right to withdraw consent. Contact us using the address above. We cannot directly inspect or delete files held only on your computer or independently by a streamer or platform.

You can complain to the [UK Information Commissioner's Office](https://ico.org.uk/make-a-complaint/). You do not have to contact us first.

## 10. Changes

We will update this notice when Pulse Weaver's data handling materially changes. The effective date above identifies the current version. If a change expands how YouTube API data is used, users will be asked to accept the updated notice before that new use.
