This is an experimental project for adding acceleration through [Windows Hypervisor Platform API](https://docs.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)
 to the [v86](https://github.com/copy/v86)-emulator implemented in JavaScript.
I wrote this over a few days, mainly to experiment with the hypervisor API. It was quite easy 
to get started with the API but I quickly needed an implementation of common PC hardware to get anything interesting running. That's when I thought of interfacing
to the excellent v86 JavaScript emulator I had recently encountered.  This emulator has the benefit the code base is very accessible so it was quite easy for me to see
how I could hook into it. I decided to put out the result in case others would find it interesting.

At least Linux 2.6 boots and runs and others are working as well. When running with this accelerator, the CPU is of course orders of magnitude faster than when running in "native JavaScript" but the I/O is slower due to the callback to the JavaScript
side that are triggered by I/O port instructions and MMIO. 

The accelerator could be useful for debugging v86 - if a bug disappears by switching to this implementation, it suggests an error in the virtual CPU implementation and not in the virtual hardware implementation.
Also it could be used for experimenting with adding 64-bit support to v86.

To you use it, you both need to use the modified v86 from the ``HyperVAccel`` branch of https://github.com/mthiim/v86, as well as the accelerator executable from this repository.
How to compile and run is explained below.

** Note! You need to enable "Windows Hypervisor Platform" in the "Turn Windows features on/off" dialogue. It is not inside the "Hyper-V" section, but further down in the dialogue.**   

# How it works

The project is a Chromium Embedded project. As such it is an application with a built-in, Chromium browser. However, the browser exposes functionality to the JS side, for providing
acceleration of the virtualization. I have then modified ``cpu.js`` file to look for this. If it exists, it will then cooperate with it so that the C++ side handles the CPU virtualization (via Hyper-V) whereas all other hardware
emulation happens in the v86 JavaScript code. My code bridges the two worlds - memory-mapped and port I/O are moved to the JavaScript side whereas interrupts are injected into the C++ side etc.
Memory is implemented as follows: The machine memory gets allocated as a big array in the C++ side and forms the memory provided to Hyper-V. But it is also used as a backing store for a JavaScript ArrayBuffer I then return to the JavaScript-side store as your CPU's "memory". Through a callback I unmap from Hyper-V any memory regions (so that they will cause exits) that are mapped via v86's mmap_register() function to allow MMIO to devices to function correctly. Likewise, I handle memory-related exits in Hyper-V by handling those to v86and same for I/O. 

The meat of the hypervisor code is in the CMachine.cpp and CMachine.h files. The binding to the JavaScript side happens in the virtual_app.h file, in the Execute() function. The binding
is explained in greater detail below.



## JavaScript API
The modified Chromium browser exposes a ``StartMachine`` function to the JavaScript code. My modified v86 branch calls this function the "CPU.create_memory"-function. The ``StartMachine`` function takes 
the desired memory size (of the machine) as a parameter, as well as references to six callback functions for handling port and MMIO mapped I/O for various byte sizes: Read1, Read2, Read4, Write1, Write2 and Write4. In response to this call, the Hypervisor
partition is created and a "machine" JavaScript object (implemented in the C++ side) is returned. 

This object has the following fields and methods:

| Key        | Type         | Description  |
|------------|--------------|--------------|
| memory      | ArrayBuffer | Array buffer containing the memory of the machine |
| parambuf      | ArrayBuffer     |   Small array buffer used for passing parameters back and forth between the C++ and JavaScript side (improves performance compared to transferring as JavaScript args) |
| run | function      | Runs the virtual machine. Takes no argument. The machine is run for a few time ticks or until it halts. The function returns the current value of RFLAGS augmented with a "HLT flag" (so the JS side can see the whether interrupts can be injected or if machine is HLT'ed, etc.). Note that callbacks to the JS side may occur in response to calling run(). |
| irq | function      | Injects an interrupt into the machine. Takes interrupt number as argument. |
| unmap | function      | "Unmaps" a specified region of physical memory. The result is that accesses to this region will thereafter trigger callbacks to the MMIO functions. The v86 code calls this function whenever MMIO regions get registered |

# How to compile the JavaScript side
Head over to my fork of v86: https://github.com/mthiim/v86. Check out the ``HyperVAccel`` branch from that repo.

The debug version doesn't require compilation, but the release version offers better performance .
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

# Running

After following the instructions above, you run the program by simply starting virtual.exe. This will open a "browser window" that automatically heads to 127.0.0.1:8000 where the pages from above are hosted. You can run
your virtual machines from there (you may need to copy in image files to the "image" directory as usual with the v86 project). Go to the "release" page for much improved performance.
  
