# Current Windows and Ubuntu testnet tester guide

Download the complete archive for your operating system from the [current testnet release](https://github.com/wizz13150/prime-gap-network/releases/tag/horizon-testnet), then extract the complete folder.

- Windows: launch `PGN-Horizon/PGN-Horizon.exe`.
- Ubuntu: launch `PGN-Horizon-Ubuntu/Horizon/launch-horizon.sh`. To compile from source, follow [BUILDING.md](BUILDING.md).

Do not launch a component-update ZIP as the application. Older alpha packages use an older testnet epoch.

1. With no wallet configured, Horizon selects Wallet and opens Wallet management automatically. Create a new testnet wallet or open an existing Core data folder there. Existing configured wallets reopen normally.
2. Wait for synchronization through the integrated encrypted connection. No VPN or exposed wallet RPC port is required.
3. Choose your public profile name. Mining and audit work require no manual admission or diagnostic recording. Core checks the signatures and submitted work automatically. Never share wallet files or credentials.
4. Turn a local GPU ON. Observe the current work role, search progress, report state and audits. Reports and audit answers survive a restart; a claim is not a credited reward. Available, pending and immature tPGN are distinct.
5. For feedback, start Diagnostics recording, reproduce the issue, stop and Save ZIP. Record GPU performance while computation is running. Include OS/driver, device, displayed versions and approximate event time. Diagnostics remain local until you send them.
6. For scientific data, use Number Lens → Native GPU observations → Export results (CSV). The archive preserves exact endpoints, gap sizes, counts and provenance. Its manifest identifies historical data that was not retained.

Updates are optional and authenticated. Choose components or the complete update and confirm application. Horizon drains its own workers and restarts the affected application when necessary; wallet data remains outside component payloads.

CPU/Huge pages and additional hardware remain wider qualification work. On Windows the account needs Lock pages in memory before a newly signed-in worker can use large pages. Running as administrator alone does not grant a missing account right. The card turns green only for actual confirmed large-page allocations. This preview has no one-click account-right setup assistant.

This supervised testnet uses play coins. Its ledger contains zero-issuance coordination records, so its height is not the Gold reward-block count. Final Gold-only chain architecture, economics, external independent operator testing and large-network load remain unfinished. Complementary pseudoprime recovery is deferred; this preview does not claim exhaustive scientific completeness against that case.

Never send wallet.dat, private keys, authentication cookies, private audit answers or entire work folders as an ordinary diagnostic report. The coordinator can observe accepted network reports and peer protocol behavior, not remotely control a tester's computer.

## Mining and startup status

CPU mining may stay OFF while GPU mining is ON. Recording Diagnostics is optional and is never a prerequisite for starting either device. The network may allocate audit service before a new search ticket; audit computation is real GPU work in the same resident worker. Follow the device role and progress rather than the “Records to submit” comparison area, whose waiting message concerns external record checking.

One connected peer can be enough for synchronization; the peer count is not a count of other miners. The current packages require no manual miner approval. Use the delivered Core component so native work validation matches the current testnet rules. Normal updates retain existing chain history and wallet data.

If a device returns from Starting to OFF, copy the status text and export a diagnostic ZIP. The current process diagnostics retain bounded launch failures, stderr and exit information from before recording started; do not send wallet files, private keys or authentication cookies. Physical SM120 startup remains subject to the tester's actual result.

## Authenticated updater recovery

In the updater SDK environment from `docs/BUILDING.md`, run `python -I tests/test_update_fetch_recovery.py` and `python -I tests/test_authenticated_updates.py`. These suites require the updater dependencies and are separate from the default CTest set. They exercise temporary HTTP failure recovery, partial-body rejection, preserved 404 handling, and authoritative TUF length/hash checks after a retry. A successful metadata check or preparation is not an installation test.
