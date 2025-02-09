# ffrunner
ffrunner is a slim Wind32 application that hosts a single windowed Netscape Plugin instance in place of an NPAPI-compatible web browser. ffrunner was made to run FusionFall with zero 3rd-party dependencies. As such, it only implements APIs used by the Unity Web Player and contains some FusionFall-specific logic and file mappings, but it can be adapted to run any generic NPAPI plugin.
- a Unicode-aware window 
- an asynchronous I/O threadpool with support for HTTP GET/POST through wininet
- configuration through command-line arguments

## Building
It is recommended to build ffrunner with mingw to minimize MSVC runtime dependencies, in which case you can build it with:
```
make
```
If you'd prefer to use MSVC, you can create a Visual Studio solution with:
```
cmake -B build
```
and find the solution in the `build` directory.

## Running
ffrunner loads the plugin file passed in through the `-m` argument.

## Known Issues
- Wine's implementation of wininet does not correctly handle HTTP resource caching
- When running FusionFall in Wine with ffrunner, scroll wheel inputs are occasionally ignored
