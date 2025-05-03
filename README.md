# elm327-j2534
J2534 Passthru driver for ELM327 devices

# Features
+ Support CAN (ISO15765) and VPW (J1850) protocols
+ Support cheap Elm327 devices with minimal fatures
+ Support periodic messages (untested)
- ELM is slow
- ELM support only one filter so it is fixed to 5E0, Mask 7F0
- IOCTL settings not implemented (only read voltage)
- Need testing and possible fixing, use at your own risk

# INSTALLATION
* Create folder C:\Elm327-J2534\
* Copy files from Driver-folder to C:\Elm327-J2534\
* Doubleclick Elm327-32bit-Windows.reg if you have 32 bit windows or Elm327-64bit-Windows.reg if you have 64 bit windows
* If you want to use another folder, set FunctionLibrary= in registry file before importing it.

# LICENSING
This is opensource, free software and uses code from other projects. 
If you want to buy me a coffee, visit:
https://universalpatcher.net/support-the-project/

