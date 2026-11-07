"""Small read-only mining summary from an already coherent Core observation.

Registered permissions and report arrivals are not measurements of online
miners or hardware capacity. No extra RPC, scheduler or consensus operation.
"""
import re


def _count(value):
    if type(value) is not int or not 0 <= value <= 2**53 - 1:
        raise ValueError("Nonnegative exact bounded network count required")
    return value


def _positions(value):
    if not isinstance(value, str) or not re.fullmatch(r"0|[1-9][0-9]{0,37}", value):
        raise ValueError("Canonical network search volume required")
    return value


def summarize(metrics, genesis):
    try:
        if (metrics.get("schema") != "PGN_NETWORK_METRICS_V1" or metrics.get("genesis") != genesis
                or not re.fullmatch(r"[0-9a-f]{64}", genesis)
                or not re.fullmatch(r"[0-9a-f]{64}", metrics.get("bestblockhash", ""))
                or metrics.get("rate_scope") != "reported_search_positions_per_chain_second_excludes_audits"
                or metrics.get("audit_validation_scope") != "historical_reports_qualified_at_snapshot_tip"):
            raise ValueError("Unknown network metrics identity or scope")
        points = metrics["points"]
        if not isinstance(points, list) or not 1 <= len(points) <= 256:
            raise ValueError("Bounded network metrics endpoint required")
        tip = points[-1]
        if (tip.get("hash") != metrics["bestblockhash"] or tip.get("height") != metrics["height"]
                or tip.get("time") != metrics["median_time_past"]):
            raise ValueError("Network summary endpoint differs from its snapshot tip")
        _count(metrics["height"])
        seconds = _count(tip["window_seconds"])
        target = _count(tip["merit_target_q16"])
        if not 30 <= seconds <= 86400 or not 1 <= target <= 64 * 65536:
            raise ValueError("Network summary units out of bounds")
        reported, audited = _positions(tip["reported_positions"]), _positions(tip["audited_positions"])
        if int(audited) > int(reported):
            raise ValueError("Audited search volume exceeds reported search volume")
        queue = metrics["audit_queue"]
        pending = sum(_count(queue[key]) for key in ("awaiting", "awaiting_seed", "assigned", "committed"))
        _count(pending)
        return dict(schema="PGN_NETWORK_SUMMARY_V1", available=True, genesis=genesis,
                    parent=metrics["bestblockhash"], height=metrics["height"],
                    registered_operators=_count(metrics["audit_enabled_operators"]),
                    reporting_operators_in_window=_count(metrics["reporting_operators_in_window"]),
                    window_seconds=seconds, reported_positions=reported, audited_positions=audited,
                    merit_target_q16=target, pending_audits=pending,
                    rate_scope="reported_search_excludes_audits_not_hardware_capacity",
                    online_miner_count=None)
    except (KeyError, TypeError, ValueError, AttributeError):
        return dict(schema="PGN_NETWORK_SUMMARY_V1", available=False,
                    reason="Coherent network summary is unavailable", online_miner_count=None)
