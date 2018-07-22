# Xmake Instructions
In order for you to successfully build with xmake, please follow the following instructions.

---

## General Configuration
You can specify the following building options

| Building Option | How to change it | Note |
|---|---|---|
| Library Type | xmake f -k \[static\|shared\] | |
| Arch Type | xmake f -a \[x86\|x64\|i386\|x86_64\|arm64\|armv5\|armv6\|armv7\|armv8\] | x86 and x64 is for Windows, i386 and x86_64 is for POSIX |
| Platform Name | xmake f -p \[windows\|macosx\|iphoneos\|linux\|freebsd\|aix\|zos\|android\|aix\|solaris\] | windows, macosx, linux, android, iphoneos will be automatically detected, other platforms need to be specified |

## After Configuration
Simply run 

```bash
$ xmake
```

After running this command, a fresh libuv will appear in the build directory. Put whereever you want it to be!