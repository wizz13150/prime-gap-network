# Preview qualification and open work

This describes a private development testnet preview. A passed check applies to the tested boundary; it does not establish production readiness or complete the Horizon roadmap.

## Verified

| Boundary | Evidence |
| --- | --- |
| Integrated public connection | A frozen Horizon TLS bridge reached the public bootstrap address and a real Core synchronized to the same block hash. No tester-side VPN route was used. Certificate and hostname verification remained enabled; wallet RPC stayed local. |
| Distributed archives | Windows ZIP and Ubuntu tar.gz were extracted and launched without a development environment. The local CPU controller appeared disabled by default; both applications closed with exit code zero. The Ubuntu archive's executable permissions were verified. |
| Wallet lifecycle | Explicit creation and reopening, integrated Core startup, synchronization and clean shutdown were exercised on Windows and Ubuntu. Existing fixture addresses were preserved. |
| UI | 32 focused C++ checks, four actual window sizes, wallet round-trip and repeated refresh samples passed for the current Windows application. These checks protect the accepted cards and layout. |
| Report publication | Real isolated nodes exercised lost replies before and after acceptance, duplicate retries, inactive competing branches, reconnection and reorganization. Inclusion was checked against the active chain. |
| Audit operators | The native selector and signed-event fixture rejected owner aliases and a second slot for the same operator, accepted another operator, and survived restart and full verification. Fixture identities were controlled test identities, not independent people. |
| CPU computation | A full local 10^13-position, five-partition validation matched the sealed reference counts. It was not an independent blind network audit. |
| CUDA residency | Four controlled native K32 pieces matched the reference. On the delivered WIZZ runtime, two consecutive 200-trillion-position tickets retained one native process and both reports were confirmed. Host dispatch measured 31 ms; this is not a CUDA kernel trace or a physical RTX 3070 Ti certification. |
| Diagnostics | Real ZIP export and rejection of forbidden private fields passed. Capture is bounded to 2 MiB in memory and export is explicit. |

The native audit-ledger reader now reconstructs its authenticated range schedule once per read. On one identical 224-report fixture, a cold read changed from 17.156 to 0.390 seconds while producing identical audit and schedule results. This is a reader measurement, not a GPU or network-throughput claim.

## Remaining limits

- A complete blind audit exchange with independent external operators, scientific acceptance and reward delivery still require end-to-end validation. Calculated results and coordination records do not imply payment.
- The former 4,096-entry history ceiling is removed. A retained 4,108-block native fixture passed cold restart, a new block, full level-four verification and clean shutdown. The combined scenario took 93.546 seconds. This does not establish large-network throughput.
- CPU auditing currently supports the 68-bit test area. Broader CPU coverage, OpenCL, final monetary rules, Gold-only currency-chain behavior, authority consensus/failover and large-network stress qualification remain open.
- Automatic recovery of an orphaned audit event onto a new parent still needs complete qualification. Pending evidence must be retained rather than claimed as accepted.
- The RTX 3070 Ti and additional physical GPU configurations need tester feedback. Unsupported hardware must be reported explicitly rather than silently using an unqualified whole-range implementation.
- Public profile-name registration, the requested leaderboard and several other UI/product roadmap items remain in development. A locally saved profile name is not proof of an on-chain identity registration.

## Artifact identities

| Component | SHA-256 |
| --- | --- |
| Windows application | `b39fad893fe6292b8b95aaa2139f806065deb97bb411ee9ef25422e21273208f` |
| Windows Core | `ee3de5ece8c857c8ecb0876bcc6a6837f02fb32d7e365e340bd00944918489b4` |
| Windows computation host | `b4be3ef2b0bfd2629dca592b18da3ee2ea9b41300cb7a73edf7c5c5147eedd3a` |
| Windows CPU controller | `559b6e59feb0180b660b339d9eed5cce53613bf235262afe000190903af7539d` |
| Ubuntu application | `d32e2c4326a31f25fbcd743d60057fae1fbd8f381149efa598faae7b78e7f379` |
| Ubuntu Core | `918cbb7a8fede3e7ad340e7a4a2c9bc62071124b3556208bb206a0453e3ae5c6` |
| Ubuntu computation host | `23a0776280a2b7adce216793498e960e7dd64a38f5b506f0eeae48a26eedb908` |
| Ubuntu CPU controller | `d26d1624b91f7e5c4bffbc5d7a4f6e4a6bad0913291768fd829a0902d2ef1195` |

Neither operating-system package contains a personal wallet, chain database, credential cookie or local scientific-history database. These artifact identities belong to the 0.1.0-alpha.1 testnet pre-release. Later UI polish candidates are separate from this frozen build.
