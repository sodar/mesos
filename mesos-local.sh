path="$HOME/local/mesos"
master_work_dir="$path/master"
slave_work_dir="$path/slave"
master_log_dir="$path/master-log"
slave_log_dir="$path/slave-log"
master_cfg_dir="$path/master-cfg"
slave_cfg_dir="$path/slave-cfg"

function run() {
	mkdir -p "$master_work_dir"
	mkdir -p "$slave_work_dir"
	mkdir -p "$master_log_dir"
	mkdir -p "$slave_log_dir"

	sudo service zookeeper start
	sudo service marathon start

	mesos-master \
		--ip=127.0.0.1 \
		--work_dir="$master_work_dir" \
		--log_dir="$master_log_dir" \
		2> /dev/null > /dev/null \
		&

	sudo mesos-slave \
		--master=127.0.0.1:5050 \
		--containerizers="docker,mesos" \
		--log_dir="$slave_log_dir" \
		2> /dev/null > /dev/null \
		&
}

function cleanup() {
	pkill -9 mesos-master
	sudo pkill -9 mesos-slave
	sudo service marathon stop
	sudo service zookeeper stop
	rm $master_work_dir/* -rf
	rm $slave_work_dir/* -rf
}

if [ $1 = run ]; then
	run
elif [ $1 = cleanup ]; then
	cleanup
elif [ $1 = restart ]; then
	cleanup
	run
else
	echo "Unknown command $1"
fi
