===========
README
===========

This branch is an attempt to recreate and expand on the work done in this paper:
Cache Simulation for Instruction Set Simulator QEMU

* `<https://ieeexplore.ieee.org/document/6945730/>`_

To read the original README file, see the master branch

* `<https://github.com/byuccl/qemu>`_

QEMU as a whole is released under the GNU General Public License,
version 2. For full licensing details, consult the LICENSE file.


Building
========

QEMU is multi-platform software intended to be buildable on all modern
Linux platforms, OS-X, Win32 (via the Mingw64 toolchain) and a variety
of other UNIX targets. 

All development in this branch has been done on Ubuntu 16.04.

The steps to build QEMU that we use are:


.. code-block:: shell

  # choose install prefix
  INSTALL_PREFIX=/home/$USER/qemu-cache/install
  mkdir build
  cd build
  # we only support arm 32 bit at this point
  ../configure --prefix=$INSTALL_PREFIX --target-list=arm-softmmu --enable-debug
  make -j4
  make install



More Information
==================

Creating Helper Functions
**************************
See the Qemu documentation on the Tiny Code Generator (TCG)

* `<https://wiki.qemu.org/Documentation/TCG>`_

Currently, we do not support ARM Thumb instructions.


Contact
=======

The QEMU community can be contacted in a number of ways, with the two
main methods being email and IRC

* `<mailto:qemu-devel@nongnu.org>`_
* `<https://lists.nongnu.org/mailman/listinfo/qemu-devel>`_
* #qemu on irc.oftc.net

Information on additional methods of contacting the community can be
found online via the QEMU website:

* `<https://qemu.org/Contribute/StartHere>`_

To contact the maintainers of this branch, try

* `<mailto:bjames@byu.net>`_
* `<mailto:jgoeders@byu.edu>`_
