"""Bounded measured progress; no URLs, filenames, credentials or guessed time."""
import time
from tuf.ngclient.fetcher import FetcherInterface

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
  count=0
  for chunk in self.inner.fetch(url):
   count+=len(chunk)
   if self.target_length:report('download',completed=min(count,self.target_length),total=self.target_length)
   yield chunk
