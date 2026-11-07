@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.36 > bootstrap.stdout 2> bootstrap.stderr
if errorlevel 1 exit /b %errorlevel%
if errorlevel 1 exit /b %errorlevel%
"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/bin/nvcc.exe" -O3 -DNDEBUG --std=c++17 --threads 1 -lineinfo -dlto -rdc=true --cudart shared -lcudadevrt -arch=sm_86 -DPGN_RELEASE_BUILD_ARCH=86 -DPGN_RELEASE_ENABLE_PUBLIC_PERIG=0 -DPGN_DESCRIPTOR_LANES=2 -DPGN_DESCRIPTOR_BLOCKS=64 -DPGN_EULER96_LAZY -DPGN_PRESIEVE_GROUPS=1 -DPGN_MARK_FULL_FAST -DPGN_MARK_BANK_ROTATE -DPGN_FUSED_SIEVE -DPGN_EULER96 -DPGN_VERIFY_GAPS -ccbin "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.36.32532/bin/Hostx64/x64" -Xcompiler=/MT -Xcompiler=/utf-8 --ptxas-options=-v search_worker.cu -o PGN-BEACON-BUCKET.exe -Xlinker=/MANIFEST:EMBED -Xlinker=/MANIFESTINPUT:worker.manifest > candidate.compile.stdout 2> candidate.compile.stderr
if errorlevel 1 exit /b %errorlevel%
exit /b 0
