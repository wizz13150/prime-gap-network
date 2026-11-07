# Current Windows testnet tester guide

Download the current r69 Windows portable archive from the release, extract the complete folder, and launch PGN-Horizon.exe. Do not launch a component-update ZIP as the application. Older alpha packages use an older testnet epoch.

1. With no wallet configuration, Horizon opens its setup assistant automatically. Choose a new testnet wallet or your existing Core data folder, review the path and confirm. Cancel leaves wallet creation unapplied. Already configured wallets reopen normally.
2. Wait for synchronization through the integrated encrypted connection. No VPN or exposed wallet RPC port is required.
3. Choose your public profile name and copy the public mining identity for the coordinator. Approval enables the normal mining/audit cycle. Peer presence alone does not grant that permission. Share only this public identity, never wallet files or credentials.
4. Turn a local GPU ON. Observe the current work role, search progress, report state and audits. Reports and audit answers survive a restart; a claim is not a credited reward. Available, pending and immature tPGN are distinct.
5. For feedback, start Diagnostics recording, reproduce the issue, stop and Save ZIP. Record GPU performance while computation is running. Include OS/driver, device, displayed versions and approximate event time. Diagnostics remain local until you send them.
6. For scientific data, use Number Lens → Native GPU observations → Export results (CSV). The archive preserves exact endpoints, gap sizes, counts and provenance. Its manifest identifies historical data that was not retained.

Updates are optional and authenticated. Choose components or the complete update and confirm application. Horizon drains its own workers and restarts the affected application when necessary; wallet data remains outside component payloads.

CPU/Huge pages and additional hardware remain wider qualification work. On Windows the account needs Lock pages in memory before a newly signed-in worker can use large pages. Running as administrator alone does not grant a missing account right. The card turns green only for actual confirmed large-page allocations. This preview has no one-click account-right setup assistant.

This supervised testnet uses play coins. Its ledger contains zero-issuance coordination records, so its height is not the Gold reward-block count. Final Gold-only chain architecture, economics, external independent operator testing and large-network load remain unfinished. Complementary pseudoprime recovery is deferred; this preview does not claim exhaustive scientific completeness against that case.

Never send wallet.dat, private keys, authentication cookies, private audit answers or entire work folders as an ordinary diagnostic report. The coordinator can observe accepted network reports and peer protocol behavior, not remotely control a tester's computer.
