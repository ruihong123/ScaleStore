//
// Created by ruihong on 9/11/24.
//

#ifndef SCALESTOREDB_MEMCACHED_H
#define SCALESTOREDB_MEMCACHED_H
#include <unistd.h>
#include <libmemcached/memcached.h>
#include <fstream>
#include <mutex>
std::string trim(const std::string &s) {
    std::string res = s;
    if (!res.empty()) {
        res.erase(0, res.find_first_not_of(" "));
        res.erase(res.find_last_not_of(" ") + 1);
    }
    return res;
}
class Memcached {
public:
    Memcached(){
        memcached_server_st *servers = NULL;
        memcached_return rc;

//        std::ifstream conf("../../memcached_ip.conf");
        std::ifstream conf("../../memcached_db_servers.conf");

        if (!conf) {
            fprintf(stderr, "can't open memcached_ip.conf\n");
        }

        std::string addr, port;
        std::getline(conf, addr);
        std::getline(conf, port);

        memc = memcached_create(NULL);
        servers = memcached_server_list_append(servers, trim(addr).c_str(),
                                               std::stoi(trim(port)), &rc);
        rc = memcached_server_push(memc, servers);

        if (rc != MEMCACHED_SUCCESS) {
            fprintf(stderr, "Counld't add server:%s\n", memcached_strerror(memc, rc));
            sleep(1);
        }

        memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
    }
    ~Memcached(){
        if (memc) {
            memcached_quit(memc);
            memcached_free(memc);
            memc = nullptr;
        }
    }

    void memSet(const char *key, uint32_t klen, const char *val,
                      uint32_t vlen) {

        memcached_return rc;
        while (true) {
            memc_mutex.lock();

            rc = memcached_set(memc, key, klen, val, vlen, (time_t) 0, (uint32_t) 0);
            if (rc == MEMCACHED_SUCCESS) {
                memc_mutex.unlock();
                break;
            } else {
                memc_mutex.unlock();

            }

            usleep(400);
        }
    }


    char *memGet(const char *key, uint32_t klen, size_t *v_size) {

        size_t l;
        char *res;
        uint32_t flags;
        memcached_return rc;

        while (true) {
            memc_mutex.lock();
            res = memcached_get(memc, key, klen, &l, &flags, &rc);
            if (rc == MEMCACHED_SUCCESS) {
                memc_mutex.unlock();
                break;
            }else{
                memc_mutex.unlock();

            }
            usleep(100); // THis has been modified.
        }

        if (v_size != nullptr) {
            *v_size = l;
        }

        return res;
    }
    memcached_st *memc = nullptr;
    std::mutex memc_mutex;
};


#endif //SCALESTOREDB_MEMCACHED_H
