#!/bin/bash

set -e

renice -n +20 $$

ulimit -c unlimited

VALGRIND='../build-linux/libtool --mode=execute valgrind --tool=callgrind --cache-sim=yes'
#VALGRIND='../build-linux/libtool --mode=execute valgrind --log-file=valgrind.out --suppressions=../build-linux/src/suppressions.valgrind --track-origins=yes'
#VALGRIND=''
SIMULATE='../build-linux/src/mesos-simulate --fake_interval=0.5 --no_create_work_dir --norequest_usage_reoffer_delay=0.001'

export GLOG_minloglevel=1
export GLOG_log_dir=/scratch/charles/mesos-logs
#export GLOG_v=1
#export GLOG_logtostderr=1

for SCENARIO in $@; do

    if test -d $SCENARIO; then
      mv $SCENARIO old/$SCENARIO.old.$(date +%s)
    fi
    mkdir $SCENARIO
    mkdir $SCENARIO/logs

    #if test x$GLOG_logtostderr != x1; then
        $SIMULATE --json_file=$SCENARIO.json --fake_extra_cpu --fake_extra_mem \
          --fake_assign_zero_cpu \
          --allocator=norequest --usage_log_base=$SCENARIO/logs/norequest. \
          >$SCENARIO/norequest.csv 2>$SCENARIO.logfile &
    #fi
    if false; then
        $SIMULATE --json_file=$SCENARIO.json --fake_extra_cpu --fake_extra_mem \
          --allocator=norequest --usage_log_base=$SCENARIO/logs/rorequest-aggressive. \
          --norequest_aggressive \
          >$SCENARIO/norequest-aggressive.csv &
    fi
    if test x$GLOG_logtostderr != x1; then
        $SIMULATE --json_file=$SCENARIO.json --fake_extra_cpu --fake_extra_mem \
          --usage_log_base=$SCENARIO/logs/simple-weak. \
          --allocator=drf >$SCENARIO/simple-weak.csv 2>$SCENARIO.logfile.sw &

    fi
done
