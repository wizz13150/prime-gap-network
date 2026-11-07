"""Shared branch binding for private recovery queues; not a verdict authority."""
import json
import re

HASH = re.compile(r'[0-9a-f]{64}\Z')


def observe_queue(database, rpc, table):
    if table not in ('answers', 'reports'):
        raise ValueError('Unsupported recovery queue table')
    tip = rpc.call('getbestblockhash')
    header = rpc.call('getblockheader', [tip])
    genesis = rpc.call('getblockhash', [0])
    height = header.get('height')
    if (not isinstance(tip, str) or not HASH.fullmatch(tip)
            or not isinstance(genesis, str) or not HASH.fullmatch(genesis)
            or type(height) is not int or height < 0):
        raise ValueError('Invalid audit recovery chain anchor')
    with database() as db:
        row = db.execute("SELECT value FROM meta WHERE key='anchor'").fetchone()
        binding = db.execute("SELECT value FROM meta WHERE key='genesis'").fetchone()
        if binding and binding[0] != genesis:
            raise ValueError('Audit catalogue belongs to another network')
        if row and len(row[0]) > 256:
            raise ValueError('Audit recovery anchor exceeds budget')
        previous = json.loads(row[0]) if row else None
        if previous and (type(previous.get('height')) is not int or previous['height'] < 0
                         or not isinstance(previous.get('hash'), str) or not HASH.fullmatch(previous['hash'])):
            raise ValueError('Invalid retained audit recovery anchor')
        if previous and previous['hash'] == tip and previous['height'] != height:
            raise ValueError('Audit anchor height changed for the same block')
        fork = bool(previous and previous['hash'] != tip and
                    (previous['height'] > height or rpc.call('getblockhash', [previous['height']]) != previous['hash']))
        if rpc.call('getbestblockhash') != tip:
            return None
        anchor = dict(hash=tip, height=height, genesis=genesis)
        if previous and previous['hash'] == tip:
            return anchor
        if fork:
            db.execute('UPDATE '+table+' SET waiting=1 WHERE waiting=0')
        db.execute("INSERT OR IGNORE INTO meta VALUES('genesis',?)", (genesis,))
        db.execute("INSERT OR REPLACE INTO meta VALUES('anchor',?)", (json.dumps(anchor, separators=(',', ':')),))
    return anchor
