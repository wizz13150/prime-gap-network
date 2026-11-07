"""Per-controller public-label persistence; no private keys or mining controls."""
from pathlib import Path
import json
import threading
from pgn_session import atomic_bytes
from public_attribution import attribution, validate_attribution
from report_commitment import canonical_bytes


class PublicProfile:
    def __init__(self, root):
        self.file = Path(root) / 'public-profile.json'
        self.lock = threading.RLock()
        self.value = attribution('PGN')
        if self.file.exists():
            if self.file.stat().st_size > 512:
                raise ValueError('Public profile exceeds capacity')
            value = json.loads(self.file.read_bytes())
            validate_attribution(value)
            self.value = value

    def name(self):
        with self.lock:
            return self.value['name']

    def update(self, control):
        if type(control) is not dict or set(control) != {'action', 'name'} or control['action'] != 'public_profile':
            raise ValueError('Exact public-profile control required')
        value = attribution(control['name'])
        raw = canonical_bytes(value) + b'\n'
        if len(raw) > 512:
            raise ValueError('Public profile exceeds capacity')
        with self.lock:
            # Publish memory only after the same atomic disk path succeeds.
            if self.value != value or not self.file.exists():
                atomic_bytes(self.file, raw)
            self.value = value
            return value['name']
