"""Qualified immutable optimized module identities, shared by startup and execution."""
import os
import re
from pathlib import Path
from pgn_native_contract import module_metadata

NATIVE_SOURCE = "96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13"
IMAGES = {
    ("windows", 86): "F0B9F81B3F8B8316BEADAB8F04E90A85C412D6DABE2793548F05D66D1AF80DD5",
    ("windows", 120): "23D8396FCBABE2955CF947BE399FFD8B707D8734BCF3BB11F97F1FC356339CED",
    ("linux", 86): "E0BC348BE993D53BDFED414A65125C8449492CB2A02C817483B24097D62DC55C",
    ("linux", 120): "80996328BEDBE84DD9DD25D5AB82EDE53897E83B21540F9A7B35378C37350859",
}


def qualified_presentation_identity(configuration, identity):
    """Read-only presentation binding; never engine or scientific admission.

    Explicit complementary sessions carry a different, qualified artifact.
    Checking just the legacy image set hides otherwise durable results. Keep
    the exact new image/source/profile and bound-feed identity together.
    """
    if (type(identity.get('worker_sha256'))is str
            and identity['worker_sha256'] in set(IMAGES.values())):
        return True
    from horizon_scientific_identity import qualified_complement_pair,TABLE_SHA
    from pgn_native_contract import PROTOCOL
    feed = configuration.get('complement_feed_sha256')
    try:
        a,b=int(configuration['start']),int(configuration['end'])
    except (KeyError,TypeError,ValueError):
        return False
    prepared=(type(feed)is str and re.fullmatch(r'[0-9a-f]{64}',feed)is not None)
    deferred=(configuration.get('complement_source_mode')=='DEFERRED_RESIDUAL_PRESEAL_V1'
              and configuration.get('complement_primary_image_sha256')==identity.get('worker_sha256')
              and configuration.get('complement_primary_source_sha256')==identity.get('worker_source_sha256')
              and configuration.get('complement_canonical_table_sha256')==TABLE_SHA)
    return (os.name=='nt' and qualified_complement_pair(
                identity.get('worker_sha256'),identity.get('worker_source_sha256'))
            and type(identity.get('profile_k'))is int and identity['profile_k']==64
            and identity.get('native_protocol')==PROTOCOL
            and (prepared or deferred)
            and configuration.get('preseal_required')is True
            and 1<<66<=a<b<=10**21)

def qualified_workers(bundle, architecture):
    platform = "windows" if os.name == "nt" else "linux"
    expected = IMAGES.get((platform, architecture))
    if expected is None:
        raise ValueError("No qualified optimized engine for this architecture")
    suffix = ".exe" if os.name == "nt" else ""
    result = []
    for profile in (64, 32, 16, 8):
        path = Path(bundle) / "bin" / f"prime-gap-worker-sm{architecture}-k{profile}{suffix}"
        metadata = module_metadata(path)
        if (metadata["image_sha256"] != expected or metadata["source_manifest_sha256"] != NATIVE_SOURCE
                or metadata["profile_k"] != profile or metadata["build_arch"] != architecture
                or metadata.get("resident_job_schema") != "SCOUT_RESIDENT_JOB_V1"):
            raise ValueError("Unqualified engine artifact; no alternate backend is admitted")
        result.append((profile, path))
    return result

