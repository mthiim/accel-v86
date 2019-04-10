This is an experimental project for adding Hyper-V acceleration support to [v86](https://github.com/copy/v86) which is a JavaScript-based x86 emulator. I wrote this over a few days, mainly to experiment with the
[Windows Hypervisor Platform API](https://docs.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform). It was quite easy 
to get started with the API but I quickly needed implementation of common PC hardware to see anything interesting running. Then I thought of "melting together"
the C cside of my Hyper-V project with the excellent v86 JavaScript emulator I had recently encountered. It has the benefit the code base is very accessible so it was quite easy for me to see
how I could hook into it. I decided to publish my result in case others would find it interesting.

At least Linux 2.6 boots and runs. The CPU is of course orders of magnitude faster than when running in "native JavaScript" but the I/O is slower due to the callback to the JavaScript
side that are triggered by I/O port instructions and MMIO. I am sure many optimizations are possible.

It could be useful for debugging v86 - if a bug disappears by switching to this implementation, it suggests an error in the virtual CPU implementation and not in the virtual hardware implementation.

** Note! You need to enable "Windows Hypervisor Platform" in the "Turn Windows features on/off" dialogue. It is not inside the "Hyper-V" option tree but further down in the dialogue.**   

# How it works

The project is a Chrome Embedded project. As such it is a web-browser, based on Chrome. However, the browser exposes special JavaScript objects and functions to JS code for providing
Hyper-V acceleration. I have then modified ``cpu.js`` file to look for this object. If it exists, it will then cooperate with it so that the C++ side handles the CPU virtualization (via Hyper-V) whereas all other hardware
emulation happens in the v86 JavaScript code. My code bridges the two worlds - memory-mapped and port I/O are moved to the JavaScript side whereas interrupts are injected into the C++ side etc.
Memory is implemented as follows: The machine memory gets allocated as a big array in the C++ side and forms the memory provided to Hyper-V. But it is also used as a backing store for a JavaScript ArrayBuffer I then return to the JavaScript-side store as your CPU's "memory". Through a callback I unmap from Hyper-V any memory regions (so that they will cause exits) that are mapped via v86's mmap_register() function to allow MMIO to devices to function correctly. Likewise, I handle memory-related exits in Hyper-V by handling those to v86and same for I/O. 


# How to compile the JavaScript side
Head over to my fork of v86: https://github.com/mthiim/v86. Check out the ``HyperVAccel`` branch from that repo.

The debug version doesn't require compilation, but the release version offers better performance (also in the integrated mode).
You can compile the release version either from Linux or from a Windows Subsystem for Linux shell (you need to have Python and unzip installed) using "make" as per the usual instructions from the v86 project. Of course you can also manually
download and run the Closure compiler - see the Makefile for the command to execute. 

After compiling, you can launch a web server:

```
python -m SimpleHTTPServer 8000
```

which will launch an HTTP server on port 8000.
 
# How to compile the C++ side

You need to have CMake, Python (2.x). Both must be on the system path. Additionally you must have Visual Studio (either 2017 or 2019) installed. Check out the project. Then create a subfolder "build" and go to it. 

From a Visual Studio command line, execute:

```
cmake -G "Visual Studio 16 2019" ..
```

(VS 2017 should also work - use "Visual Studio 15 2017").

This will result in a Visual Studio project being generated - the file is called: cef.sln. Open it in Visual Studio and compile. This will result in a "Virtual.exe" file being created in the "Virtual\Debug" subdirectory
underneath "build". For better performance, compile the project in release mode.

You run the program by simply starting virtual.exe. This will open a "browser window" that automatically heads to 127.0.0.1:8000 where the pages from above are hosted. You can run
your virtual machines from there (you may need to copy in image files to the "image" directory as usual with the v86 project). Go to the "release" page for much improved performance.
  
