"""Bounded measured progress and HTTP failure recovery before any body bytes.

Progress packets omit URLs and credentials. Final HTTP errors identify only
the public server and basename, never URL user information or query values.
"""
import time
import uuid
from pathlib import PurePosixPath
from urllib.parse import urlsplit,urlunsplit
from tuf.ngclient.fetcher import FetcherInterface
from tuf.api.exceptions import DownloadHTTPError

_sink=None
_last={}
def configure(sink):
 global _sink,_last
 _sink=sink;_last={}
def report(stage,component='',completed=0,total=0,*,force=False):
 if _sink is None:return
 if stage not in ('metadata','download','verify','prepare','drain','startup','committed'):raise ValueError('Unknown update progress stage')
 if component not in ('','ui','core','gpu','cpu'):raise ValueError('Unknown progress component')
 if type(completed)is not int or type(total)is not int or not 0<=completed<=total<2**53:raise ValueError('Invalid measured progress')
 key=(stage,component);now=time.monotonic()
 if not force and completed!=total and now-_last.get(key,-1)<.15:return
 _last[key]=now
 _sink(dict(schema='PGN_UPDATE_PROGRESS_V1',stage=stage,component=component,completed=completed,total=total))

class ProgressFetcher(FetcherInterface):
 def __init__(self,inner):self.inner=inner;self.target_length=0
 def _fetch(self,url):
  count=0;current=url
  for attempt in range(2):
   try:
    for chunk in self.inner.fetch(current):
     count+=len(chunk)
     if self.target_length:report('download',completed=min(count,self.target_length),total=self.target_length)
     yield chunk
    return
   except DownloadHTTPError as error:
    parsed=urlsplit(url)
    if attempt==0 and count==0 and error.status_code in (500,502,503,504):
     # Do not concatenate a second response after a partially delivered body.
     # A fresh first-hop GitHub URL also avoids reusing a cached gateway error.
     # TUF still authenticates exactly the same target bytes and signatures.
     if parsed.scheme=='https'and parsed.hostname in ('github.com','raw.githubusercontent.com')and not parsed.query:
      current=urlunsplit((parsed.scheme,parsed.netloc,parsed.path,'pgn_retry='+uuid.uuid4().hex,''))
     time.sleep(.5)
     continue
    host=parsed.hostname or 'update server';name=PurePosixPath(parsed.path).name[:180]
    raise DownloadHTTPError(f'Update server {host} returned HTTP {error.status_code} while fetching {name}; retry later.',error.status_code)from error
