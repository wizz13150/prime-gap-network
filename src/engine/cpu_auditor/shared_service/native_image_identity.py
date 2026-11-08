"""Exact native images compiled into this controller's build identity.

The source build tool generates this module in its isolated freeze-source
folder after compiling and testing the Linux worker. Installed runtime files
and command-line options cannot override these hashes.
"""
HASHES={
    'nt':('2745fd47c0bfd9314bb97fb4f666252c3aebd62ea3d2b40460fa21d3f18acf70',
          '6df31ea78036ed3ef8ab0b5b8f70401252fdfaa9cd940ccdca6a445a2ac619f0'),
    'posix':('e7002438518160cbfdc3eeb12f702a3183c93432340ca2a4cb1378d6ac76c0d3',
             '4f853f8ac6bd6466f109d6538c2a85c5c046994b6323283f7340afb41877fa4e'),
}
