# Core13 / GPU62: audit availability on the current testnet

Core13 enables the testnet audit-operator rule at candidate block16064, only
for genesis0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d.
Earlier heights keep their original rules. Clients must update Core as well
as the controller before using this testnet rule; mainnet is unchanged.

The selector closes partially committed reports first and prefers other
operators where possible. Networks with one or two operators can use own
or repeated-operator checks. Such checks are explicitly distinguished from
two independent operators. Ownership ignorance is not guaranteed: public
intervals can be recognized from the producer's history.

Signatures, parent/slot/attempt binding, commit/reveal, exact result checks,
existing financial conservation and duplicate-payment rejection remain in
place. No reward schedule change is included. The two regular audits and
at most two catch-up audits per ticket are retained.

The exact configured-rule boundaries, signed histories, rollback/replay,
retained pre-activation receipts and settlement seams were tested with the
native Windows and Ubuntu executables. These tests do not claim10,000-node
throughput or make self-checks independent.
