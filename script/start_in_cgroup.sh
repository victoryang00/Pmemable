#!/bin/bash
$1 &

sudo echo `echo $!` > /sys/fs/cgroup/memory/pmem_re/cgroup.procs

sudo echo $((8 * 1024 * 1024)) > /sys/fs/cgroup/memory/pmem_re/memory.limit_in_bytes