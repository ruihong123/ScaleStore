#!/bin/bash
bin=`dirname "$0"`
bin=`cd "$bin"; pwd`
SRC_HOME=$bin/..
BIN_HOME=$bin/../build/frontend
bin_dir=$BIN_HOME
#home_dir="/users/Ruihong/ScaleStore/"
nmemory="10"
ncompute="10"
nmachines="20"
nshard="10"
numa_node=("0" "1")
port=$((10000+RANDOM%1000))
bin=`dirname "$0"`
bin=`cd "$bin"; pwd`

core_dump_dir="/mnt/core_dump"
github_repo="https://github.com/ruihong123/ScaleStore"
gitbranch="reserved_branch1"
function run_bench() {
  communication_port=()
#	memory_port=()
	memory_server=()
  memory_shard=()

#	compute_port=()
	compute_server=()
	compute_shard=()
#	machines=()
	i=0
  n=0
  while [ $n -lt $nmemory ]
  do
    memory_server+=("node-$i")
    i=$((i+1))
    n=$((n+1))
  done
  n=0
  i=$((nmachines-1))
  while [ $n -lt $ncompute ]
  do

    compute_server+=("node-$i")
    i=$((i-1))
    n=$((n+1))
  done
  echo "here are the sets up"
  echo $?
  echo compute servers are ${compute_server[@]}
  echo memoryserver is ${memory_server[@]}
#  echo ${machines[@]}
  n=0
  while [ $n -lt $nshard ]
  do
    communication_port+=("$((port+n))")
    n=$((n+1))
  done
  n=0
  while [ $n -lt $nshard ]
  do
    # if [[ $i == "2" ]]; then
    # 	i=$((i-1))
    # 	continue
    # fi
    compute_shard+=(${compute_server[$n%$ncompute]})
    memory_shard+=(${memory_server[$n%$nmemory]})
    n=$((n+1))
  done
  echo compute shards are ${compute_shard[@]}
  echo memory shards are ${memory_shard[@]}
  echo communication ports are ${communication_port[@]}
#  test for download and compile the codes
  i=1
  for node in ${compute_shard[@]}
  do
    echo "Rsync the $node rsync -a $home_dir $node:$home_dir"
    rsync -a $home_dir $node:$home_dir
    ssh -o StrictHostKeyChecking=no $node "cd ${bin_dir}' | ./Occupier" &

  done

  for node in ${memory_shard[@]}
  do
    echo "Rsync the $node rsync -a $home_dir $node:$home_dir"
    rsync -a $home_dir $node:$home_dir
    ssh -o StrictHostKeyChecking=no $node "cd ${bin_dir}' | ./Occupier" &
  done


	}
	run_bench