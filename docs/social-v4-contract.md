# Social V4 — review contract, not an activated service

B971-SOCIAL-TAB-20261110 / P3 ·45 remains open until complete operator validation.
V3 and the all-white V4 rendering were rejected. The operator accepted the subsequent mixed palette/composition as the direction to continue, not as final DA or global mission acceptance. It preserves dark Horizon chrome/rails/commands and uses soft neutral reading/editor surfaces only. No deployment, social service or crypto protocol is selected here.

## Presentation and one source format

The operator's real Discord screenshot determines the composition: channel rail, central conversation, grouped author/time/avatar fallback, collapsible participants and fixed composer. Text channels and participants are optional authenticated read-model fields; absence is unavailable, never invented. Initials are a local avatar fallback derived from the supplied display name; no avatar, media, presence or user is fetched implicitly.

[Discord keyboard navigation](https://support.discord.com/hc/en-us/articles/1500000056121-Keyboard-Navigation-FAQ) supports navigation between sections with F6/Shift+F6, lists with arrows and activation with Enter/Space; [Windows shortcuts](https://support.discord.com/hc/en-us/articles/225977308--Windows-Discord-Hotkeys) documents Ctrl+U for the member list. Qt native controls implement these interactions. The component retains Tab and Escape. Incoming data must not focus an editor, open a reader or select a channel.

The retained Ghost and Discourse mechanisms continue: publications feed → full reading → dedicated writing/preview. The operator rejected a monolithic Forum reader; the revised Forum uses a category rail and structured topics (author, authenticated reply count, activity and state), then distinct posts with avatar fallback, author/time, quote, reply, source-provided reactions and per-post moderation. [Discourse](https://meta.discourse.org/t/understanding-discourse-for-new-users/96331) defines category/topic/post and contextual replies/quoting; [GitHub Discussions](https://docs.github.com/en/discussions/collaborating-with-your-community-using-discussions/participating-in-a-discussion) documents original comments, reply threads and emoji reactions. Votes, reputation, automatic moderation and answered badges are not imported.

Forum quote/reply opens its dedicated BBCode source composer and matching preview. Reply drafts retain the canonical target post within their topic; no send is connected. Thread paging is a separate read-model callback bound to identity/genesis/topic/cursor, at most50 replies per response and200 loaded replies. Duplicate/overlapping pages, stale scope, wrong topic and malformed data are rejected; a failed page retains existing posts. Each page still requires authenticated same-network provenance from the adapter. Topic metadata and reaction counts are optional source fields: absent remains unavailable, never a derived total or zero. The loaded-reply count is explicitly separate from the service total. Counts are not optimistically changed on a reaction request.

The eight main review renders now use the operator's requested native sizes,1366×950 and1920×1030, with the shared Horizon primary-navigation style. Separate full-size Forum thread renders demonstrate the post journey. They are native QWidget renders of the production component in a marked fixture harness, not stretched images or an activated connected application.

After structural review the operator rejected the uniform gray table rendering as dated. The subsequent visual revision keeps that structure and mixed palette: topic titles and creator byline, restrained category/state pills, source-backed contributor-avatar groups, relative activity plus last contributor, denser rows, subtle hover and a teal focus marker instead of a blue rectangle. The inline topic action menu is functional and has Menu/Shift+F10 alternatives. Contributor identities are explicit optional service fields, capped at8; when absent the UI can show only the known topic author, not invented participants. Exact UTC activity and supplied contributor identities remain in the safe tooltip. Relative time is presentation of the supplied timestamp, never a presence or freshness assertion. This visual revision remains subject to operator review.

Miners remains the exact four-column source-backed table with explicit window/sort, unavailable distinct from zero and no score/podium.

### Publications versus an announcement channel — decision pending

The operator questioned whether Publications duplicates a moderator-only or announcement channel. All four tabs remain until an explicit decision.

| Need | Simple announcement channel | Dedicated Publications |
| --- | --- | --- |
| Short network updates by moderators | A text channel with service-enforced posting rights suffices. Read rights and write rights are separate. | Adds navigation/editorial maintenance without a proven need for this use alone. |
| Titled long-form notes, references, full reading and draft preview | Possible as messages, but title/reference/editorial lifecycle are not separate fields in the current Chat subset. | The existing title/body/optional ticket-Gold reference and dedicated reading/preview fit this distinct need. |
| Publishing to followers in other communities | [Discord announcement channels](https://support.discord.com/hc/en-us/articles/360032008192-Announcement-Channel-FAQ) and [Channel Following](https://support.discord.com/hc/en-us/articles/360028384531-Channel-Following-FAQ) are a service feature with explicit Publish/Follow behavior. | No such distribution service is connected or promised. A second view must not imply crossposting. |

Inference for Root review: if the actual need is only moderator announcements, prefer one service-backed content source/channel over two independently editable feeds. If authored long-form notes are required, retain the [Ghost-style editorial flow](https://ghost.org/help/posts/) and decide whether announcements are a filtered presentation of the same canonical content. This is a comparison, not an implemented merge, tab removal, reserved channel or new distribution protocol.

[Fluent color](https://fluent2.microsoft.design/color) explains neutral surfaces and hierarchy; [Carbon color/contrast](https://www.carbondesignsystem.com/building-blocks/foundations/color/overview) distinguishes surface/text/control roles and minimum contrast. Operator-selected values are dark #041319/#09262d, conversation #dfe7e7 and editor #eaf0ef, with anthracite content text. [Microsoft typography](https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/typography) supports a 14px body and secondary 12px text. The values are an operator-directed adaptation, not a claim that Carbon defines a teal theme. QSS is local to SocialView; PGN/Wallet and shared Horizon helpers are not edited.

### Chat BBCode subset

The source stored in drafts and supplied to any future send adapter is BBCode, never generated HTML or a second Markdown document. Toolbar commands insert tags into the same QPlainTextEdit that accepts manual input. Preview and conversation use the same renderer. Preview never rewrites the source.

No SCEditor code or assets are imported; its [MIT license](https://raw.githubusercontent.com/SamClarke/SCEditor/master/LICENSE.md) is recorded for provenance. Its browser/JavaScript prerequisites do not apply to this Qt-native implementation. phpBB/Discord/Fluent/Carbon are documentation references; their products, backend terms and licences are not substituted for the project's existing Qt obligations.

| Toolbar | Exact source form | Reading behavior |
| --- | --- | --- |
| B | [b]text[/b] | Bold |
| I | [i]text[/i] | Italic |
| U | [u]text[/u] | Underline |
| S | [s]text[/s] | Strike-through |
| Quote | [quote]text[/quote] | Indented block; nesting stays bounded |
| Code | [code]literal source[/code] | Monospace; inner tags remain literal |
| Link | [url=https://example.org]label[/url] | Underlined label with visible target; no activation |
| Manual URL | [url]https://example.org[/url] | Visible URL; no activation |

Tag conventions come from the official [phpBB BBCode guide](https://www.phpbb.com/community/help/bbcode). The official [SCEditor BBCode format](https://www.sceditor.com/documentation/formats/bbcode/) demonstrates source conversion and explicit parser options. This native renderer deliberately does not import its HTML conversion/auto-repair path.

Only paired, properly nested supported tags are interpreted, case-insensitively. Unknown tags remain literal. If recognized tags are crossed/unclosed, or depth/token limits are exceeded, the complete message is displayed literally. Code blocks are opaque. Limits: 4000 UTF-16 source units for Chat and forum replies,16000 for forum topic bodies, depth16,1024 bracket tokens; display beyond the relevant source limit has an explicit truncation notice. The same tag subset is used by the Forum editor and preview; authenticated forum posts state whether their source is BBCode or literal text. Toolbar insertion refuses to exceed the source limit; quoting refuses an oversized excerpt rather than silently cutting a tag. Drafts are isolated by identity+genesis and explicit channel (up to16 channel drafts); the earlier unassigned draft remains separate.

URL targets must be absolute HTTP(S), have a host, no embedded credentials and <=2048 characters. Unsafe schemes stay literal. No images, attachments, font/color/size tags, arbitrary HTML or remote/local resources are supported. Literal ampersands, angle brackets and entity-looking text are inserted exactly once using QTextCursor, not setHtml. QTextDocument resource loading is denied, including file URLs. Qt's [rich text subset](https://doc.qt.io/qt-6/richtext-html-subset.html), [loadResource](https://doc.qt.io/qt-6/qtextdocument.html#loadResource) and [QTextBrowser link behavior](https://doc.qt.io/qt-6/qtextbrowser.html#openExternalLinks-prop) explain why importing arbitrary HTML or relying on link defaults would not meet this contract. Native text formatting does not execute a web engine. No remote resource service contract has been approved.

## Roles, members and permissions

The operator's invariant is **Moderator is the highest role throughout all Social**: chat, publications, forum and leaderboard. No hidden Social admin/owner role, no miner approval or mining diagnostic authorization is introduced.

Initial moderator references supplied by Root:

| Person | Public script reference |
| --- | --- |
| Wizz / local | 00140b8cb565270a3ede7f09ae1ecdd7e0eaa7430e8d |
| BenXY | 0014366e689f0f4538bf6684af4194061ef30653559a |
| Robert | 0014ac89b12dbb52858e2e1343b093b5bba359b77618 |

These are public bootstrap references only. They are not hardcoded into permission logic, not proof of key possession and not automatically bound to display names. Root must verify the canonical authenticated account mapping and perform the effective service bootstrap before activation. Visual harness names are explicitly synthetic and grant no product rights.

[Discord Roles and Permissions](https://support.discord.com/hc/en-us/articles/214836687-Discord-Roles-and-Permissions) documents roles below the actor's highest role, restrictions on granting rights the actor does not possess, and moderation of strictly lower members. [Discord API permissions](https://docs.discord.com/developers/topics/permissions) documents base permissions, bitsets and channel overwrites. Position is not general permission inheritance: channel role allow/deny aggregation is distinct from hierarchy. It would be wrong to use highest role alone as the ACL engine.

Selected review semantics and unresolved differences:

- Multiple roles and an explicit order are displayed from service data. The designated Moderator role must exist and no role may be above it in a candidate snapshot.
- Role creation/edit/removal/movement, assignments and member moderation require service permission and current hierarchy checks. Protected actions are never authorized by the UI.
- Discord's documented strict-lower rule means peer Moderators cannot kick/ban one another or edit a role equal to their own highest role. Any desired peer-management exception needs explicit operator arbitration and service policy; the client does not invent one.
- Discord has a guild-owner exception and an Administrator permission that bypasses channel restrictions. Those cannot silently be imported as a hidden role above Moderator. Exact bootstrap, changes to the top role and loss-of-all-moderators governance remain Root decisions.
- Channel permissions have Allow / Inherit / Deny data. The service computes effective rights using its selected, documented policy. The UI never resolves permission conflicts.
- Context menus expose local copy and service-protected moderation on real target IDs. Chat/feed/forum content actions are scoped to their section and channel. The leaderboard exposes identity/provenance/member actions; no delete/approve/recompute metric action exists. Native context-menu access and keyboard activation accompany mouse access. No action is authorized by display name.

### Mature backend comparison

| Candidate | Documented capabilities | Mismatch with exact Discord semantics |
| --- | --- | --- |
| Discord service itself | Native guild roles, ordered positions, permission bitsets and channel/member overwrites, enforcement and moderation. | External Discord identities/service/terms; guild owner and Administrator exceptions conflict with an unqualified “Moderator is the only top authority” invariant. Public PGN scripts are not Discord auth. No Discord service has been selected. |
| [Matrix power levels](https://spec.matrix.org/v1.19/client-server-api/#mroompower_levels), [room authorization rules](https://spec.matrix.org/v1.19/rooms/v12/) | Mature room state, send/state/kick/ban thresholds, server authorization and persistent membership. | One numeric user level per room is not Discord's multiple roles plus ordered hierarchy and role/member overwrite aggregation. Version12 additionally gives room creators infinite power that cannot be demoted: this is a concrete mismatch with the selected Moderator-top invariant. Labeling power50 “Moderator” does not repair either difference. Custom room events/ACL middleware require independent review. |
| [Keycloak roles/composites and fine-grained management](https://www.keycloak.org/docs/latest/server_admin/index.html) | Mature identity, sessions, role mappings, composite roles and admin authorization. | Composite inheritance is not Discord role position or channel overwrites. Keycloak is an identity-provider candidate, not an implemented Discord social backend. Operational realm administration must be explicitly governed, never disguised as a hidden Social role. |

No homegrown ACL engine has been implemented. The structs are an injection boundary, not a wire protocol or server implementation.

### Frontend/backend action contract

An authenticated access snapshot is bound to canonical identity+genesis+community, an opaque policy revision, source/time, a verified non-secret session reference, roles, members, channel overwrites and **service-computed per-target availability**. Local bounds are100 roles,200 displayed members,500 overwrites,128 permission names and1000 availability entries. Service pagination/completeness remains required beyond those limits.

Role/member/channel controls prepare an AccessRequest naming identity, genesis, community, section/channel, target and optional parent topic, role, expected policy revision, non-secret session reference, operation and payload. A request UUID correlates a result; it is not a cryptographic challenge or idempotency protocol. The adapter must map it to canonical service transactions, using only the payload fields defined for the selected operation. Editing a role does not also move its position. Permission labels are readable UI text; canonical permission IDs remain distinct data.

For every mutation the service MUST independently authenticate the session, check revocation/current policy, bind the tenant/genesis/target, enforce permitted rights and hierarchy, reject stale revisions and forged fields, apply the action atomically, and retain an audit result. It must reject a crafted request even if the client fabricated “allowed=true”. The UI's availability list is presentation and a fail-closed preflight only.

Results are Accepted / Denied / Conflict / Unavailable with safe public reason and matching request/scope. No optimistic permission assignment. Accepted/conflict invalidates local permissions until a refreshed authenticated snapshot. Late/duplicate callbacks and changes of identity/network/channel cannot restore authority. Whether an action is idempotent, how policy revisions are stored and how audit logs are retained are canonical backend obligations, not decisions of this component.

## Persistent identity, devices and moderator recovery

The service owns a persistent canonical account and the Moderator assignment. Display name, draft JSON, client-selected identity/genesis and a public address cannot grant or restore rights. Drafts contain no permissions, passkeys, tokens, assertions or recovery codes. Reopening the component on the same or a new machine starts without verified permissions until the service supplies them.

The injection model distinguishes Not verified, Signing in, Verified by service, Recovery required, Credential revoked and Session expired. Account/device controls only invoke identity-provider flows offered by the adapter. No WebAuthn challenge, assertion or private key is handled in this UI. A Verified status requires identity binding, expiry and public verification/revocation references; these are assertions from the trusted adapter, not cryptography performed by the client.

The primary [WebAuthn verification procedure](https://www.w3.org/TR/webauthn-3/#sctn-verifying-assertion) requires a relying party to validate the ceremony type, fresh matching challenge, allowed origin/RP ID, user presence/verification flags and signature over authenticator data and client data hash. Credential/account binding, single-use challenge consumption, revocation and replay handling must survive service restart. A signature-counter anomaly is a risk signal, not universally proof of cloning; synced passkeys can have different counter behavior.

[Keycloak WebAuthn/passkeys and credential management](https://www.keycloak.org/docs/latest/server_admin/index.html) is a mature candidate for these ceremonies. Authentication must use its supported flow and server verification; the Social client must not invent an alternative signature scheme. Its configuration, relying-party origin, secure redirects, account binding, session validation/revocation and claim-to-Social identity mapping remain Root qualification items.

Recovery choices for Root review, based on [FIDO passkeys](https://fidoalliance.org/passkeys/) and [FIDO deployment/recovery guidance](https://fidoalliance.org/wp-content/uploads/2024/05/Synced-Passkey-Deployment_-Emerging-Practices-for-Consumer-Use-Cases_2024-Final.pdf):

1. Register a second independent FIDO authenticator while already strongly authenticated. A lost device can then be revoked after service-verified login on the backup; persistent account and Social role do not change.
2. Synced passkeys can help on a new device, but recovery also depends on the passkey provider account. Device-bound credentials need an independently enrolled backup. Losing every authenticator/provider account is not automatically recoverable.
3. [Keycloak recovery codes](https://www.keycloak.org/docs/latest/server_admin/index.html) are documented one-time backup **second-factor** credentials. They are not automatically a complete passwordless account recovery solution and are not proof of PGN script possession. Root must choose the whole flow, storage, anti-replay and revocation policy.
4. An operator-assisted recovery route would introduce trust in identity-provider operations. It must be explicitly approved and audited; no hidden “super moderator” or username override is implemented. In the absence of an approved route and surviving proof, recovery stays denied.

Local tests can verify the adapter/UI boundaries: public-only identity is denied; stale/revoked/unverified sessions and cross-network results fail closed; no rights persist in draft files or across a fresh component; service Denied is displayed; duplicate/out-of-order callbacks cannot re-elevate state. They **cannot certify actual passkey signature verification, challenge persistence or backend enforcement while no real service exists**. Before activation Root must qualify spoofed assertions, bad signatures, wrong origin/RP/challenge, challenge replay, credential revocation, restart/new-device login and approved recovery end-to-end against the selected mature provider.

## Review boundary

Representative data belongs only to test executables with a visible review indicator. No fixture is linked into PGN-Horizon. Windows/Ubuntu native component tests and inspected renders qualify only the tested local behavior. Root owns actual backend selection, secure authenticated bootstrap, integration and activation; the operator accepts the complete result and may request further corrections.
