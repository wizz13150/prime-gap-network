# Social workspace — V4 review candidate

B971-SOCIAL-TAB-20261110 remains open until the operator validates the complete result. V3 and the all-white V4 preview were rejected. The mixed dark Horizon chrome / soft neutral reading surfaces are the accepted direction for this pass.

See [social-v4-contract.md](social-v4-contract.md) for primary sources, BBCode tags/limits, roles/member/channel semantics, bootstrap references, persistent identity and recovery, service obligations, and the unresolved Publications-versus-announcements decision.

The product adds Social at primary index2 while preserving PGN0 and Wallet1. Chat, Publications, Forum and Miners remain four existing-scope sections. QSS is local to SocialView; shared Horizon navigation, PGN and Wallet styles are unchanged.

Chat has authenticated channel/participant injection, avatar initials fallback, one conversation, a fixed BBCode source editor and matching preview. No voice/DM, implicit media fetch or real send service is introduced. Local drafts are atomic, bounded and isolated by identity+genesis+channel; forum replies remain per-topic.

Publications separates feed/full reading/writing/preview. Forum separates categories/topics/reading/reply composition. Miners uses four exact columns, source provenance, exact numeric sorting, announced windows and bounded loaded pages. Missing is unavailable, never zero inferred.

Role/member/channel actions and account/device flows are injectable, fail-closed interfaces. The UI does not compute Discord ACLs, authenticate signatures, store permissions, autoassign moderators or invent recovery. Moderator is the highest Social role, with no mining approval system. Real writes, authentication, server authorization and secure bootstrap remain Root responsibilities.

The standalone tests/social_component project builds only Social and the native test. The review harness alone contains synthetic fixtures and displays a review indicator. --render outputs eight native main views at1366x950 and1920x1030; --render-forum shows the distinct post thread; --render-access supplies separately marked role/member/channel/account evidence. No fixture is linked into PGN-Horizon.

A passing local test is not proof of the selected backend, cryptographic verification, integrated application or operator acceptance. No activation, publication or live runtime change is part of this worktree.
