#!/bin/bash
sysctl -w vm.nr_hugepages=36864
ulimit -l unlimited