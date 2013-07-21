#
#Android modem dependency analyzer
#
adepends.py, utility for analysing android module dependency

There are thousands of modules within android system. They form a very 
complicated dependency graph. When we want to learn about a particular module 
in android system, it's not easy to find out where modules that are dependent 
on by the module we mainly focus on are located. To make this task easier, I 
wrote adepends.py, which can be used to analysis dependency relationship 
between modules. It's capable of:

List modules defined within a directory
Generate a graphviz dot based diagram file to show dependency relationship
To show which modules are defined in a directory, we can use this command: "
adepends.py -l DIRECTORY_NAME". For example, if we run "adepends.py -l 
external/protobuf/" command, we get below output:

libprotobuf-java-2.3.0-micro
libprotobuf-cpp-2.3.0-lite
host-libprotobuf-java-2.3.0-micro
libprotobuf-cpp-2.3.0-full
host-libprotobuf-java-2.3.0-lite
aprotoc
libprotobuf-java-2.3.0-lite

To generate a dependency diagram for a particular module, we can use this 
command: "adepends.py -o output.dot -m module_name". For example, if we're 
interested in the dependency diagram for charger module,  we can use this 
command: "adepends.py -o output.dot -m charger". After the command finished, 
we have output.dot in current directory. Then we run "dot -Tpng -ooutput.png 
output.dot" to generate a png file for the diagram. And the diagram is shown 
below:



Each ellipse represents a module, the top line shows the module name, and the 
bottom line shows the directory that the module is defined. The arrowed edge 
represents the dependency relationship between two modules.

#
#Android Crash Symbol Analyzer
#
ease android stacktrace examination in vim with agdb

Continue the post view call stack of crashed application on android.
The agdb tool is able to convert PC register to source file information, but 
not convenient enough. After agdb outputs source file information, we still 
need to open the source file in vim and jump to the corresponding line 
manually.

Now agdb tool can generate a vim specific error file for the stacktrace to 
help us jump to source code directly.

Still consider the stacktrace below:


22 F/ASessionDescription(   33): frameworks/base/media/libstagefright/rtsp/
ASessionDescription.cpp:264 CHECK_GT( end,s) failed:  vs. 

23 I/DEBUG   (   30): *** *** *** *** *** *** *** *** *** *** *** *** *** *** 
*** ***

24 I/DEBUG   (   30): Build fingerprint: 'generic/generic/generic:2.3.1/
GINGERBREAD/eng.raymond.20101222.130550:eng/test-keys'

25 I/DEBUG   (   30): pid: 33, tid: 450  >>> /system/bin/mediaserver <<<
26 I/DEBUG   (   30): signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 
deadbaad

27 I/DEBUG   (   30):  r0 deadbaad  r1 0000000c  r2 00000027  r3 00000000
28 I/DEBUG   (   30):  r4 00000080  r5 afd46668  r6 40806c10  r7 00000000
29 I/DEBUG   (   30):  r8 8031db1d  r9 0000fae0  10 00100000  fp 00000001
30 I/DEBUG   (   30):  ip ffffffff  sp 40806778  lr afd19375  pc afd15ef0  
cpsr 00000030

31 I/DEBUG   (   30):          #00  pc 00015ef0  /system/lib/libc.so
32 I/DEBUG   (   30):          #01  pc 00001440  /system/lib/liblog.so
33 I/DEBUG   (   30): 
34 I/DEBUG   (   30): code around pc:
35 I/DEBUG   (   30): afd15ed0 68241c23 d1fb2c00 68dae027 d0042a00 
36 I/DEBUG   (   30): afd15ee0 20014d18 6028447d 48174790 24802227 
37 I/DEBUG   (   30): afd15ef0 f7f57002 2106eb56 ec92f7f6 0563aa01 
38 I/DEBUG   (   30): afd15f00 60932100 91016051 1c112006 e818f7f6 
39 I/DEBUG   (   30): afd15f10 2200a905 f7f62002 f7f5e824 2106eb42 
40 I/DEBUG   (   30): 
41 I/DEBUG   (   30): code around lr:
42 I/DEBUG   (   30): afd19354 b0834a0d 589c447b 26009001 686768a5 
43 I/DEBUG   (   30): afd19364 220ce008 2b005eab 1c28d003 47889901 
44 I/DEBUG   (   30): afd19374 35544306 d5f43f01 2c006824 b003d1ee 
45 I/DEBUG   (   30): afd19384 bdf01c30 000281a8 ffffff88 1c0fb5f0 
46 I/DEBUG   (   30): afd19394 43551c3d a904b087 1c16ac01 604d9004 
47 I/DEBUG   (   30): 
48 I/DEBUG   (   30): stack:
49 ........................ 
92 I/DEBUG   (   30):     408067e4  6f697470
93 I/BootReceiver(   75): Copying /data/tombstones/tombstone_09 to DropBox (
SYSTEM_TOMBSTONE)


We run this command to convert it to vim error file:
agdb.py -v < tombstone_01.txt > vim_error_file
# the -v argument tells agdb to generate a vim error file

Then in vim, we run :cg vim_error_file to load the error file to vim's 
quickfix buffer.

After the error file is loaded, we run :cw command in vim to examine the call 
stack. It's shown below:


1 pid: 33, tid: 450  >>> /system/bin/mediaserver <<<
2 /path_to_android_src_root/bionic/libc/unistd/abort.c:82: #00 
__libc_android_abort

3 /path_to_android_src_root/system/core/liblog/logd_write.c:235: #01 
__android_log_assert

Now we can focus on the line that we'd like to further investigate, and press 
enter. Vim will bring us the the exact line that the error corresponds to.


The idea of the feature is very simple. As we vim users know, vim is able to 
recognize lots of compilers' error message. The agdb tool converts the format 
of stacktrace to gcc compiler's error format, then vim can load the output to 
quickfix and associate lines with corresponding source code.

For example:
cat tombstone_00 |./agdb.py --android-src-root=/opt/workspace/mmp2_devel/ --
product-name=phoenix  -r

tombstone_00 is a file pulled from /data/tombstones/tombstone_00 


