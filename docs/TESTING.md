# Private testnet tester guide

Windows: extract the full archive and open PGN-Horizon.exe.
Ubuntu 24.04 x86-64 desktop: extract the full archive, then run ./Horizon.
No VPN or development tools are required. Horizon opens its encrypted testnet connection itself.

1. Open Wallet and select Set up wallet. Choose the normal user-data folder, another folder, or an existing wallet. Creation starts only after Finish.
2. Wait for synchronization. The initial download and verification can take several minutes. Ledger height and synchronization state are shown in the footer.
3. Open Profile, choose your public name or pseudonym, and copy the Public audit address for the testnet coordinator. The coordinator must admit auditor identities; this does not reserve a range.
4. Switch a local GPU card ON to request random network work automatically. CUDA images for SM86 and SM120 and the optimized 67–95-bit range are included. The RTX3070Ti belongs to the SM86 family; that particular card still needs tester feedback. An appropriate installed NVIDIA driver is required.
5. CPU cards operate as auditors, with a selectable dedicated thread budget. The CPU arithmetic currently supports the 68-bit testnet area. ON without assigned compatible work means waiting, not active calculation.
6. Diagnostics is a separate tool in the Profile menu. Start recording before reproducing a problem, stop, then Save ZIP. The capture is limited to 2 MiB in memory and excludes wallet files, keys, credentials, audit answers and gap tables.

Scope of this preview
This is a development testnet with play coins. Search results, committed reports, completed audits, scientific acceptance and rewards are separate states. The current coordination ledger includes zero-issuance records: its height is not the number of Gold mining blocks. Pending reports remain available until processed. A report needs two admitted audit operators distinct from its owner; with fewer eligible operators, it stays pending while other mining work continues. No quorum is silently lowered.

Please report: OS and NVIDIA driver version, which device was ON, what happened, the public audit address when admission is needed, and an optional diagnostic ZIP. Do not send private keys or wallet files.

The preview is intended for a small private trial. Large-network load, final Gold-only currency-chain rules, final economics and the remaining Horizon roadmap are still under development. It is not a production launch or a claim that all hardware has been physically tested.

Coordinator presence inspection is available in `tools/inspect_testnet.py`. It reads local authenticated RPC and reports peer IDs, announced height, synchronization and traffic counters. It never sends a message, enrolls an identity or requests work. A connection count alone does not establish independent audit operators.
