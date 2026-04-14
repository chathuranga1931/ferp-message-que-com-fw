
Sming installation V4.5
==================

Go to https://sming.readthedocs.io/en/latest/getting-started/windows/index.html Sming installation readthedocs.
Run command in Quick Install with cmd admin rights.
Wait.
For testing, go to Sming sample project in cmd.(C:\tools\Sming\samples\Basic_Blink). To compile run, 
  make

Gulp Web pack installation
==========================

The file optimization uses ``gulp``. To install it and the needed gulp
packages you need to install first `npm <https://www.npmjs.com/>`__. Npm
is the Node.JS package manager. Once you are done with the installation
you can run from the command line the following:

npm install

The command above will install gulp and its dependencies.

If got error "ReferenceError: primordials is not defined" do this. Create new file here as 'npm-shrinkwrap.json' and put his in it.
    {
      "dependencies": {
        "graceful-fs": {
            "version": "4.2.2"
         }
      }
    }
then run the command npm install

Usage
=====
* To chage comport number run, 
  make COM_PORT=COM5

* To increase bausdrate for programming run,
  make COM_SPEED_ESPTOOL=982100

* To do all compile and uploading, run,
  make combine

* To Read 4MB flash, run 
  make readflash

* To export and copy to "export" directory the build bin files, run
  make exportbuild   

* using flash download tool, write those bin files according to those starting addresses
  rboot.bin                   0x00000000
  partitions.bin              0x003fa000
  rom0.bin                    0x00002000
  spiff_rom.bin               0x00200000
  esp_init_data_default.bin   0x003fc000

Try   make help   for more.  

Release version
===============
* To make release version which stops debug prints over Serial Debug run,
  make SMING_RELEASE=1

* To make debug version run,
  make SMING_RELEASE=0

* Release version removes project serial debug prints.

* run this to print debug outputs over system serial output. once run this, the variable remembers
  make clean
  make DEBUG_VERBOSE_LEVEL=DBG
          DBG - debug outputs
          INFO - information outputs
          WARN - warning outputs

* run this to switch debug prints from Serial0 to Serial2(debug output)  
  make clean
  make DEBUG_VERBOSE_LEVEL=INFO

* To remove all debug outputs set false in Serial.systemDebugOutput()
