import sys,unittest
from pathlib import Path
from unittest import mock
from urllib.parse import urlsplit
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/update'))
from tuf.api.exceptions import DownloadHTTPError
import horizon_update_progress as progress

class ScriptedFetcher:
 def __init__(self,*responses):self.responses=list(responses);self.urls=[]
 def fetch(self,url):
  self.urls.append(url);response=self.responses.pop(0)
  for value in response:
   if isinstance(value,Exception):raise value
   yield value

class RecoveryTests(unittest.TestCase):
 def tearDown(self):progress.configure(None)
 @mock.patch.object(progress.time,'sleep')
 def test_gateway_failure_recovers_without_changing_body(self,sleep):
  inner=ScriptedFetcher([DownloadHTTPError('gateway',504)],[b'ab',b'c'])
  client=progress.ProgressFetcher(inner);client.target_length=3;events=[];progress.configure(events.append)
  self.assertEqual(b''.join(client.fetch('https://github.com/owner/repo/file.zip')),b'abc')
  self.assertEqual(len(inner.urls),2);a,b=map(urlsplit,inner.urls)
  self.assertEqual((a.scheme,a.netloc,a.path),(b.scheme,b.netloc,b.path))
  self.assertTrue(b.query.startswith('pgn_retry='));self.assertEqual(events[-1]['completed'],3)
  sleep.assert_called_once_with(.5)
 @mock.patch.object(progress.time,'sleep')
 def test_partial_response_is_never_replayed(self,sleep):
  inner=ScriptedFetcher([b'first',DownloadHTTPError('gateway',504)],[b'duplicate'])
  stream=progress.ProgressFetcher(inner).fetch('https://github.com/owner/file.zip')
  self.assertEqual(next(stream),b'first')
  with self.assertRaises(DownloadHTTPError)as caught:next(stream)
  self.assertEqual(caught.exception.status_code,504);self.assertEqual(len(inner.urls),1);sleep.assert_not_called()
 @mock.patch.object(progress.time,'sleep')
 def test_expected_missing_root_does_not_retry(self,sleep):
  inner=ScriptedFetcher([DownloadHTTPError('missing',404)])
  with self.assertRaises(DownloadHTTPError)as caught:b''.join(progress.ProgressFetcher(inner).fetch('https://raw.githubusercontent.com/owner/2.root.json'))
  self.assertEqual(caught.exception.status_code,404);self.assertEqual(len(inner.urls),1);sleep.assert_not_called()
 @mock.patch.object(progress.time,'sleep')
 def test_repeated_failure_is_bounded_and_does_not_expose_secrets(self,sleep):
  inner=ScriptedFetcher([DownloadHTTPError('temporary',503)],[DownloadHTTPError('still unavailable',502)])
  with self.assertRaises(DownloadHTTPError)as caught:b''.join(progress.ProgressFetcher(inner).fetch('https://user:secret@github.com/owner/file.zip?token=private'))
  self.assertEqual(len(inner.urls),2);self.assertEqual(caught.exception.status_code,502)
  message=str(caught.exception);self.assertIn('github.com',message);self.assertIn('file.zip',message)
  for secret in ('user:','secret','private','token='):self.assertNotIn(secret,message)
  self.assertEqual(inner.urls[0],inner.urls[1]);sleep.assert_called_once()

if __name__=='__main__':unittest.main()
