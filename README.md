bytecode2firm - a Java bytecode front end for libFirm.
======================================================

1. Introduction
---------------

bytecode2firm is a compiler that translates java bytecode to machine code. Code generation is done with the libfirm intermediate representatin and backend.
It currently is a static (ahead of time) compiler.

2. Building
-----------

Make sure you have libfirm and liboo in the current directory. If you have a
git checkout you can do:

	$ git submodule update --init

Then you can do the usual

	$ make

This builds bytecode2firm along with our simple testing runtime.

3. libgcj runtime
-----------------

This step is optional.

If you want as much Java library as possible, you need libgcj on your system.
Install gcj. bytecode2firm needs libgcj.so, and the precompiled runtime classes
from gcj's rt.jar. In Ubuntu, install "libgcj-10 gcj-4.4-jre-lib".
(Hint: you might want to make sure that gcj is not your default Java VM
afterwards. See "update-alternatives --config X", where
X = {java, javac, javah, ...})

For the initial setup, you can execute a script (you might need to adjust the
paths used in that script).

	$ ./setup_runtime_gcj.sh

4. Running
----------

Make sure build/bytecode2firm is in your PATH. A typical invokation looks like:

	$ javac Main.java
	$ bytecode2firm -cp . Main

There is also a little testsuite at
	http://pp.ipd.kit.edu/git/bytecode2firm-testsuite/

5. Contact
----------

You can contact us at
	<firm@ipd.info.uni-karlsruhe.de> 
