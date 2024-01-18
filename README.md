# boost-asio-utils
This repo contains the infrastructure files required to develop the application code based on Boost::ASIO library in C++.

# Download instructions

git clone git@github.com:sandeep2181/boost-asio-utils.git

# Host dependencies
This build is tested on Ubuntu 22.04. Before starting the build make sure that mosquitto broker and boost libraries are installed in your machine.

# Host Build instructions
cd boost-asio-utils/
cmake .. && make
mkdir build_host && cd build_host

