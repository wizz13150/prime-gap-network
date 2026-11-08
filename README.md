# Prime Gap Network
### Community-driven prime-gap research

Prime Gap Network (PGN) is a collaborative initiative conceived by **Rob Smith**, known as **robert44444uk** in the prime-gap community. Robert develops the scientific and network rules; Wizz works on the software implementation.

[Community website](https://wizz13150.github.io/prime-gap-network.github.io/) · [Discord](https://discord.gg/qZ2DerDMXQ) · [Standalone Scout](https://github.com/wizz13150/prime-gap-scout)

## Two separate applications

**Prime Gap Scout** is the standalone prime-gap search tool. It needs no wallet, blockchain or PGN network. Its releases and console documentation belong to the [Scout repository](https://github.com/wizz13150/prime-gap-scout).

**PGN Horizon** is the network client in this repository: an integrated wallet, optimized search engines, automatic work tickets, independent audits, and views of network evidence. It owns its engine runtime and does not require an installed Scout application.

## Windows network test — UI 2026.11.8.76

[Download Windows x64](https://github.com/wizz13150/prime-gap-network/releases/download/horizon-testnet-updates-epoch2/PGN-Horizon-testnet-Windows-x64.zip) · [Release and checksums](https://github.com/wizz13150/prime-gap-network/releases/tag/horizon-testnet-updates-epoch2) · [Tester guide](docs/TESTING.md)

This is the current portable Windows preview for supervised tests on the PGN testnet. Use this epoch's package when joining; older alpha packages belong to an older test network.

1. Extract the complete folder and launch **PGN-Horizon.exe**. PGN Core and the engine runtimes are included.
2. On the first launch, the wallet setup assistant opens automatically. Create a testnet wallet or select an existing Core data folder. Review the folder and confirm; cancellation does not create a wallet. Existing configured wallets reopen normally.
3. Let the integrated encrypted connection synchronize. No VPN or public wallet RPC port is required.
4. In Profile, copy your **public mining identity** and send it to the testnet coordinator for approval. A connected peer is not automatically an admitted miner/auditor. Never send wallet files, keys or RPC credentials.
5. Turn your GPU on. The normal work cycle handles assigned audits, searches, report publication and validation. There is no separate audit-only product mode.

Existing r69 clients can update the Interface from **Software updates → Download selected → Restart and install**. The r69→76 update and Core reconnection have been tested on Windows; a successful update does not yet restore mining automatically in r76.

### What this preview provides

- Optimized NVIDIA CUDA search images for SM86/SM120 and 67–95-bit intervals, with four memory profiles. The current network search domain begins inside the 68-bit band and ends at 2⁹⁵, exclusive.
- Automatic tickets, durable audit commitments, report disclosure and independently assigned checks. Discovery, audit acceptance and payment are shown as separate states.
- A real testnet wallet using **tPGN**, including available and immature balances, send/receive and wallet management. There is no conversion into mainnet PGN.
- Best/Recent results, shared device cards, ticket/audit evidence, network history, and an Atlas with a graduation for every bit in its complete-domain view.
- Optional signed updates for the interface, Core, GPU runtime and CPU runtime. The updater verifies payloads and preserves wallet data; updates are not forced. `components-26.zip` is an updater asset, not the portable application download.
- Manual diagnostic ZIP export and scientific CSV export in **Number Lens → Native GPU observations → Export results (CSV)**. Exact gap endpoints, gap-size counts and provenance are retained for completed searches. Historical missing populations are disclosed, never invented.

### Qualification and remaining scope

Controlled laboratory clients have exchanged blind audit commitments and revealed matching answers. A normal isolated controller recovered its retained work and automatically published an accepted Gold, crediting **171 immature tPGN** to its wallet. These are controlled laboratory identities, not independently operated human testers or exhaustive power-loss testing.

CPU network operation/Huge pages and physical SM120 performance need wider qualification. Windows Huge pages require the account's **Lock pages in memory** right and a new sign-in; the assisted privilege-grant flow is not included. The CPU card reports real allocation status rather than assuming it is enabled.

The test ledger includes coordination records without coin issuance. **Ledger height is not Gold-block count.** Final Gold-only chain architecture, final economics and large-network load qualification remain development work. Complementary pseudoprime recovery is deferred in this preview; exhaustive scientific completeness against that case is not claimed. Ordinary network audits and exact Gold verification remain enabled.

**Ubuntu source verification (2026-11-07):** [Download the complete Ubuntu source](https://github.com/wizz13150/prime-gap-network/releases/download/horizon-testnet-updates-epoch2/PGN-Horizon-testnet-Ubuntu-source.zip). A clean Ubuntu 24.04 build of every Ninja target and all 245 CTest tests pass with Qt 6.4.2 and GCC 13.3. This is source to compile, not an Ubuntu binary. [Build instructions](docs/BUILDING.md).

## Search performance

Reference measurements for the retained active computation pipeline on an **RTX 3090**, at **1500 MHz core / 5001 MHz memory**:

| Search width | Positions per second |
| --- | ---: |
| 68 bits | 1,003.52 billion |
| 80 bits | 782.51 billion |
| 95 bits | 575.23 billion |

A position is an integer search position advanced, not a primality test. These are component reference measurements, not promises for a different GPU or for end-to-end network throughput including auditing, reporting and waiting. Scout documents the complete bit-by-bit reference table.

## Why prime gaps?

A prime gap is the distance between consecutive prime numbers. Searching for unusually large gaps combines mathematical questions with substantial computational work. PGN aims to coordinate that effort, preserve evidence, and make the results accessible to researchers and volunteer contributors.

The network design separates discovery from verification. A returned range report is committed before its audit answers are disclosed; accepted scientific results require the configured independent checks. A displayed candidate or a completed local ticket does not by itself establish network acceptance or payment.

## Building and reviewing the software

The application source is in `src/`; the Core fork is in `wallet/riecoin-core-upstream-v2606/`. Qualified optimized CUDA input sources are in `src/engine/prime_gap_native/`, accompanied by their input manifest. This snapshot excludes retired independent search prototypes from the runtime source export.

See [build instructions and provenance](docs/BUILDING.md) and [preview qualification](docs/QUALIFICATION.md). Build outputs require their own qualification before replacing the pinned runtime images. A successful compilation is not a new performance or compatibility certification.

Wallets, private keys, authentication cookies, local mining history and operator-specific runtime configuration are not repository contents.

## Background and community records

The software effort began in late July 2026 as an update to Gapcoin software, then expanded to work involving Riecoin and Primecoin. Contact with Robert opened the collaboration around his PGN initiative.

The [development copy of the historical community range register](data/community/README.md) is retained for provenance. Existing records remain under their maintainers' stewardship. They are distinct from automatic PGN tickets; Horizon does not offer manual network reservations.

## Get involved

Researchers, developers and volunteer testers are welcome. Introduce yourself in the [PGN Discord community](https://discord.gg/qZ2DerDMXQ). For a private trial, share only your public audit address and diagnostic information requested by the coordinator. Never send a private key or wallet file.

## Acknowledgements

- **Rob Smith (robert44444uk)** — originator of PGN and its scientific and network design.
- **Wizz** — software implementation and integration.
- The upstream Riecoin, Bitcoin, Gapcoin and mathematical software contributors whose work underpins the project. Existing component licence and copyright notices are retained in their respective source trees and distribution notices.

