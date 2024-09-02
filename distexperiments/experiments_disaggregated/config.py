from distexprunner import ServerList, Server


SERVER_PORT = 20005


server_list = ServerList(
#----------------------Purdue------------------------
    # compute nodes fill in
    Server('node-0', '10.145.21.34', SERVER_PORT, ssdPath="/dev/shm"),
    Server('node-1', '10.145.21.35', SERVER_PORT, ssdPath="/dev/shm "),


    # memory nodes fill in
    Server('node-2', '10.145.21.36', SERVER_PORT, ssdPath="/dev/shm"),
    Server('node-3', '10.145.21.39', SERVER_PORT, ssdPath="/dev/shm"),

#----------------------cloudlab----------------------
    # compute nodes fill in
    # Server('node-0', 'node-0', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-1', 'node-1', SERVER_PORT, ssdPath="/dev/shm "),
    # Server('node-2', 'node-2', SERVER_PORT, ssdPath="/dev/shm"),
    #
    #
    # # memory nodes fill in
    # Server('node-3', 'node-3', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-4', 'node-4', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-5', 'node-5', SERVER_PORT, ssdPath="/dev/shm"),



    # Server('node-6', 'node-6', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-7', 'node-7', SERVER_PORT, ssdPath="/dev/shm"),
    #
    #
    # Server('node-8', 'node-8', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-9', 'node-9', SERVER_PORT, ssdPath="/dev/shm "),
    # Server('node-10', 'node-10', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-11', 'node-11', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-12', 'node-12', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-13', 'node-13', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-14', 'node-14', SERVER_PORT, ssdPath="/dev/shm"),
    # Server('node-15', 'node-15', SERVER_PORT, ssdPath="/dev/shm"),
)
