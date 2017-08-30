qeventjson - streaming Grid Engine events in JSON
==================================================
This repository provides `qeventjson`, a command-line utility for Sun/Oracle Grid Engine that provides a real-time JSON feed of cluster updates, such as starting and finishing jobs.

The utility provides the back-end for the web-based cluster status viewer of the Biomedical Imaging Group Rotterdam (BIGR). It connects to the grid engine manager and subscribes to cluster event updates. This real-time stream provides updates on job submissions, starting and finishing jobs, resource usage and several other interesting statistics. `qeventjson` converts these events to JSON objects for further processing. (For example, the BIGR status viewer uses this to show a real-time overview of jobs running on the cluster.)

How to compile
--------------
`qeventjson` uses the SGE libraries to connect to the cluster manager, so it needs to be compiled as part of the full Grid Engine source. This repository is a copy of the `GE2011.11p1.tar.gz` with a number of patches that add the `qeventjson` utility in the `source/clients/qeventjson` directory.

Assuming that all libraries required by SGE are installed, compilation can be done by running:
```bash
cd source/
./aimk -only-depend -no-java -no-jni -no-secure -no-dump
scripts/zerodepend
./aimk -no-java -no-jni -no-secure -only-core -no-dump depend
./aimk -no-java -no-jni -no-secure -only-core -no-dump
```
This will produce a `qeventjson` binary in `source/LINUXAMD64` (or similar directory for your architecture).

Usage
-----
Run `qeventjson` to get running updates of the cluster status. This will first print the current status, listing all current jobs, and will then provide updates when jobs start, end or when new jobs are submitted. It will print one JSON object per line, with some debugging information on lines prefixed with `#`. See the `qeventjson.c` source code and the Grid Engine event documentation for a full list of the events and the structure of the JSON objects.

About
-----
Most of the code in this repository is from the `GE2011.11p1.tar.gz` release of Open Grid Scheduler/Open Grid Engine (http://gridscheduler.sourceforge.net/).

The `qeventjson` addition was developed by Gijs van Tulder at the Biomedical Imaging Group Rotterdam, Erasmus University Medical Center, Rotterdam, the Netherlands, in 2015 -- 2016.
* http://www.bigr.nl/
* http://vantulder.net/
