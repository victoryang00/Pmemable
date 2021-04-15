#!/bin/bash

sudo echo $1 > /sys/fs/cgroup/memory/pmem_re/cgroup.procs

sudo echo $((8 * 1024 * 1024)) > /sys/fs/cgroup/memory/my_cgroup/memory.limit_in_bytes