#! /usr/bin/env bash
bin=`dirname "$0"`
bin=`cd "$bin"; pwd`
SRC_HOME=$bin/..
BIN_HOME=$bin/../build/frontend
bin_dir=$BIN_HOME
conf_file_all=$bin/../connection_cloudlab.conf
conf_file=$bin/../connection.conf
# alpha = 1/(1-theta)
#compute_nodes=$bin/compute_nodes
#memory_nodes=$bin/memory_nodes
log_file=$bin/log
cache_mem_size=8 # 8 gb Local memory size
remote_mem_size_base=48 # 48 gb Remote memory size
#master_ip=db3.cs.purdue.edu # make sure this is in accordance with the server whose is_master=1
master_port=12311
port=$((10000+RANDOM%1000))

#workernum=8
dramGBCompute=8
dramGBMemory=32 #32
ssdGBCompute=9
ssdGBMemory=36 #36
numberNodes=$(($compute_num+$memory_num))
#zipf=0 #[0~1]
probSSD=100
pp=2 # default 2
fp=1
messagehdt=4 # default 4
RUNS=1
Runtime=40
ssdPath="/mnt/core_dump/data.blk"
#core_dump_dir="/mnt/core_dump"
#numacommand="numactl --physcpubind=23" #bind to 1 core
#numacommand="numactl --physcpubind=22,23" #bind to 2 core
#numacommand="numactl --physcpubind=20,21,22,23" # bind to 4 cores

#numacommand="numactl --physcpubind=18,19,20,21,22,23" # bind to 4 cores
numacommand="" # no limit on the core.

run() {
    echo "run for result_file=$result_file,
    thread=$thread, zipfian_alpha=$zipfian_alpha, workload=$workload,
    remote_ratio=$remote_ratio, shared_ratio=$shared_ratio,
    read_ratio=$read_ratio, op_type=$op_type,
    space_locality=$space_locality, time_locality=$time_locality"

#    compute_line_all=$(sed -n '1p' conf_file_all)
#    memory_line_all=$(sed -n '2p' conf_file_all)
    awk -v pos="$node" -F' ' '{
        for (i=1; i<=NF; i++) {
            if (i <= pos) {
                printf("%s", $i)
                if (i < pos) printf(" ")
            }
        }
        print ""
    }' "$conf_file_all" > "$conf_file"

    old_IFS=$IFS
    IFS=' '
    compute_line=$(sed -n '1p' $conf_file)
    memory_line=$(sed -n '2p' $conf_file)
    read -r -a compute_nodes <<< "$compute_line"
    read -r -a memory_nodes <<< "$memory_line"
    compute_num=${#compute_nodes[@]}
    memory_num=${#memory_nodes[@]}
    echo "memory nodes:"
    i=0
    for memory in "${memory_nodes[@]}"
    do
       echo $memory
       i=$((i+1))
    done
    i=0
    echo "compute nodes:"
    for compute in "${compute_nodes[@]}"
    do
       echo $compute
       i=$((i+1))
    done

    j=0
    for compute in "${compute_nodes[@]}"
    do
      ip=`echo $compute | cut -d ' ' -f1`
      ssh -o StrictHostKeyChecking=no $ip "killall micro_bench > /dev/null 2>&1 && cd $BIN_HOME"
      j=$((j+1))
  #		if [ $j = $node ]; then
  #			break;
  #		fi
    done

    j=0
      for memory in "${memory_nodes[@]}"
      do
        ip=`echo $memory | cut -d ' ' -f1`
        ssh -o StrictHostKeyChecking=no $ip "killall memory_server_term > /dev/null 2>&1 && cd $BIN_HOME"
        j=$((j+1))
  #  		if [ $j = $node ]; then
  #  			break;
  #  		fi
      done

    i=0
    while [ $i -lt $memory_num ]
    do
      echo "Rsync the connection.conf to ${memory_nodes[$i]}"
      rsync -vz /users/Ruihong/ScaleStore/connection.conf ${memory_nodes[$i]}:/users/Ruihong/ScaleStore/connection.conf
      i=$((i+1))
    done
    i=0
    while [ $i -lt $compute_num ]
    do
      echo "Rsync the connection.conf to ${compute_nodes[$i]}"
      rsync -vz /users/Ruihong/ScaleStore/connection.conf ${compute_nodes[$i]}:/users/Ruihong/ScaleStore/connection.conf
      i=$((i+1))
    done
    i=0
#    compute_nodes_arr=`cat "$compute_nodes"`
#    memory_nodes_arr=`cat "$memory_nodes"`
#    echo ${#memory_nodes[@]}
#    echo ${#memory_nodes[@]}

#    compute_num=$(wc -l < $compute_nodes)
#    memory_num=$(wc -l < $memory_nodes)
#    compute_num=$((compute_num+1))
#    memory_num=$((memory_num+1))
#    echo `cat $slaves`
    echo $compute_num
    echo $memory_num
    numberNodes=$(($compute_num + $memory_num))
  	read -r -a memcached_node <<< $(head -n 1 $SRC_HOME/memcached_ip.conf)
  	echo "restart memcached on ${memcached_node[0]}"
    ssh -o StrictHostKeyChecking=no ${memcached_node[0]} "sudo service memcached restart"

    if [ $size_grow = 1  ]; then
      remote_mem_size=$(($remote_mem_size_base*$compute_num))
    else
      remote_mem_size=$remote_mem_size_base
    fi

    for memory in "${memory_nodes[@]}"
        do
          ip=$memory
#            	port=`echo $memory | cut -d ' ' -f2`
          ibip="192.168.100.$((i+compute_num+1))"
          ssh ${ssh_opts} $memory "sudo ifconfig ib0 $ibip"
          script_memory="cd ${bin_dir} && $numacommand ./MemoryServer -worker=$thread -dramGB=$dramGBMemory -nodes=$numberNodes -messageHandlerThreads=$messagehdt   -ownIp=$ibip -pageProviderThreads=$pp -coolingPercentage=10 -freePercentage=$fp -csvFile=ycsb_data_scalability_new_hashtable.csv -tag=NO_DELEGATE -evictCoolestEpochs=0.5 --ssd_path=$ssdPath --ssd_gib=$ssdGBMemory -prob_SSD=$probSSD > $log_file.$ip 2>&1"
          echo "start worker: ssh ${ssh_opts} ${memory} '$script_memory' &"
          ssh ${ssh_opts} ${memory} " sudo touch /mnt/core_dump/data.blk; sudo chown -R Ruihong:purduedb-PG0 /mnt/core_dump; echo '$core_dump_dir/core$memory' | sudo tee /proc/sys/kernel/core_pattern"
          ssh ${ssh_opts} ${memory} "ulimit -S -c unlimited &&  $script_memory" &
          i=$((i+1))
#        	if [ "$i" = "$node" ]; then
#        		break
#        	fi
        done # for slave
    sleep 2
    i=0
    for compute in "${compute_nodes[@]}"
      do
        ip=$compute
        if [ $i = 0 ]; then
          is_master=1
              master_ip=$ip
        else
          is_master=0
        fi
#        if [ $port == $ip ]; then
#          port=12345
#        fi

        ibip="192.168.100.$((i+1))"
        ssh ${ssh_opts} $compute "sudo ifconfig ib0 $ibip"
        script_compute="cd ${bin_dir} && ./microbench -worker=$thread -dramGB=$dramGBCompute -nodes=$numberNodes -messageHandlerThreads=$messagehdt   -ownIp=$ibip -pageProviderThreads=$pp -coolingPercentage=10 -freePercentage=$fp -evictCoolestEpochs=0.5 --ssd_path=$ssdPath --ssd_gib=$ssdGBCompute -prob_SSD=$probSSD  -all_workloads=true -zip_workload=$workload -zipfian_param=$zipfian_alpha -space_locality=$space_locality -shared_ratio=$shared_ratio -allocated_mem_size=$remote_mem_size"

        echo "start worker: ssh ${ssh_opts} ${compute} '$script_compute | tee -a $log_file.$ip' &"
        ssh ${ssh_opts} ${compute} "sudo chown -R Ruihong:purduedb-PG0 /mnt/core_dump; sudo touch /mnt/core_dump/data.blk ; echo '$core_dump_dir/core$compute' | sudo tee /proc/sys/kernel/core_pattern"
        ssh ${ssh_opts} ${compute} "ulimit -S -c unlimited && $script_compute | tee -a $log_file.$ip" &
        i=$((i+1))
      done # for compute

	  wait


    sleep 1
    IFS="$old_IFS"
}


run_thread_test() {
# thread test
echo "*********************run thread test**********************"
result_file=$bin/results/thread
node_range="8"
thread_range="1 2 3 4 5 6 7 8"
remote_range="0 50 100"
shared_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
read_range="0 50 100"
space_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
time_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.15

for remote_ratio in $remote_range
do
for op_type in $op_range
do
for read_ratio in $read_range
do
for shared_ratio in $shared_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}


run_remote_test() {
# remote test
echo "**************************run remote test****************************"
result_file=$bin/results/remote_ratio
node_range="8"
thread_range="1"
remote_range="0 10 20 30 40 50 60 70 80 90 100"
shared_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
read_range="0 100" #"0 50 100"
space_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
time_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.15

for op_type in $op_range
do
for read_ratio in $read_range
do
for shared_ratio in $shared_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for remote_ratio in $remote_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}


run_shared_test() {
# shared test
echo "**************************run shared test****************************"
result_file=$bin/results/shared_ratio
node_range="8"
thread_range="1"
remote_range="88"
shared_range="0 10 20 30 40 50 60 70 80 90 100"
read_range="50" #"0 50 70 80 90 100"
space_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
time_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.15

for op_type in $op_range
do
for read_ratio in $read_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for remote_ratio in $remote_range
do
for shared_ratio in $shared_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}

run_shared_test_noeviction() {
# shared test
echo "**************************run shared test****************************"
result_file=$bin/results/shared_ratio-noeviction
node_range="8"
thread_range="1"
remote_range="88"
shared_range="0 10 20 30 40 50 60 70 80 90 100"
read_range="50 100" #"0 50 70 80 90 100"
space_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
time_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.5

for op_type in $op_range
do
for read_ratio in $read_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for remote_ratio in $remote_range
do
for shared_ratio in $shared_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}


run_read_test() {
# read ratio test
echo "**************************run read ratio test****************************"
result_file=$bin/results/read_ratio
node_range="8"
thread_range="1"
remote_range="0 50 100"
shared_range="0"
read_range="0 10 20 30 40 50 60 70 80 90 100"
space_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
time_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.15

for remote_ratio in $remote_range
do
for op_type in $op_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for shared_ratio in $shared_range
do
for read_ratio in $read_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}

run_space_test() {
# space locality test
echo "**************************run space locality test****************************"
result_file=$bin/results/space_locality
node_range="8"
thread_range="1"
remote_range="100"
shared_range="0"
read_range="0 50 100"
space_range="0 10 20 30 40 50 60 70 80 90 100"
time_range="0" #"0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.15

for remote_ratio in $remote_range
do
for op_type in $op_range
do
for read_ratio in $read_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for shared_ratio in $shared_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}


run_time_test() {
# time locality test
echo "**************************run time locality test****************************"
result_file=$bin/results/time_locality
node_range="8"
thread_range="1"
remote_range="100"
shared_range="0"
read_range="0 50 100"
space_range="0"
time_range="0 10 20 30 40 50 60 70 80 90 100"
op_range="0 1 2 3"
cache_th=0.15

for remote_ratio in $remote_range
do
for op_type in $op_range
do
for read_ratio in $read_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for shared_ratio in $shared_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
}


run_node_test() {
# node test
echo "**************************run node test****************************"
result_file=$bin/results/node
node_range="8"
thread_range="1 2 4 8 16"
remote_range="100"
shared_range="100"
size_grow=0 # 0 not grow, 1 grow with node number
read_range="100"
space_range="0"
time_range="0"
workload_range="0" # 0 uniform, 1 single zipfian, n >1 multispot zipfian.
zipfian_alpha_range="0.99" #make sure workload = 1 if we want to test zipfian.
#
op_range="1" # use 1
#cache_th=0.5
for workload in $workload_range
do
for zipfian_alpha in $zipfian_alpha_range
do
for remote_ratio in $remote_range
do
for shared_ratio in $shared_range
do
  if [ $shared_ratio != 100 && $size_grow=1]; then
      exit
  fi
for op_type in $op_range
do
for read_ratio in $read_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do

for thread in $thread_range
do
for node in $node_range
do
  echo $node
#    remote_ratio=`echo "($node-1)*100/$node" | bc`
#    echo $remote_ratio
#    if [[ $node = 1 ]]; then
#        continue;
#    fi
  run
done
done
done
done
done
done
done
done
done
done
}

#run_thread_test
#run_read_test
#run_time_test
#run_shared_test
#run_remote_test
#run_space_test
#run_shared_test_noeviction
run_node_test
