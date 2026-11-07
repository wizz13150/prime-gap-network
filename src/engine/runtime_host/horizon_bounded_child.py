"""Bounded pipes for a finite, single-process native proof helper."""
import os
import subprocess
import threading
import time
import math


def run(command, request, *, timeout=40, stdout_limit=4*1024*1024, stderr_limit=16384):
    if (not isinstance(request, bytes) or len(request)>1024*1024
            or type(timeout) not in (int,float) or not math.isfinite(timeout) or timeout<=0
            or type(stdout_limit) is not int or not 0<stdout_limit<=4*1024*1024
            or type(stderr_limit) is not int or not 0<stderr_limit<=16384):
        raise ValueError("Invalid native proof transfer budget")
    options = {"creationflags": 0x08000000} if os.name=="nt" else {}
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, **options)
    buffers = [bytearray(), bytearray()]
    failed = threading.Event()
    errors = []

    def read(stream, buffer, limit):
        try:
            while True:
                chunk = stream.read(min(65536, limit+1-len(buffer)))
                if not chunk:
                    return
                buffer.extend(chunk)
                if len(buffer)>limit:
                    raise ValueError("Native proof output exceeds budget")
        except (OSError, ValueError) as error:
            errors.append(error)
            failed.set()

    def write():
        try:
            process.stdin.write(request)
        except BrokenPipeError:
            pass
        except OSError as error:
            errors.append(error)
            failed.set()
        finally:
            try:
                process.stdin.close()
            except OSError:
                pass

    threads = [threading.Thread(target=read,args=(process.stdout,buffers[0],stdout_limit),daemon=True),
               threading.Thread(target=read,args=(process.stderr,buffers[1],stderr_limit),daemon=True),
               threading.Thread(target=write,daemon=True)]
    deadline = time.monotonic()+timeout
    try:
        for thread in threads:
            thread.start()
        while process.poll() is None:
            if failed.wait(min(.025,max(0,deadline-time.monotonic()))):
                raise errors[0]
            if time.monotonic()>=deadline:
                raise subprocess.TimeoutExpired(command,timeout)
        process.wait()
    finally:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=3)
        for thread in threads:
            if thread.ident is not None:
                thread.join(1)
        if any(thread.is_alive() for thread in threads):
            raise RuntimeError("Native proof helper pipes did not drain")
        for stream in (process.stdin,process.stdout,process.stderr):
            try:
                stream.close()
            except OSError:
                pass
    if errors:
        raise errors[0]
    return subprocess.CompletedProcess(command,process.returncode,bytes(buffers[0]),bytes(buffers[1]))
