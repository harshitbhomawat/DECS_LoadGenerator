#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <string>
#include <random>
#include <unordered_map>
#include "httplib.h"

using namespace std;
using namespace chrono;

enum class Workload {
    PUT_ALL,
    GET_ALL,
    POPULAR_GET,
    MIXED
};

Workload parse_workload(const string &s) {
    if (s == "putall") return Workload::PUT_ALL;
    if (s == "getall") return Workload::GET_ALL;
    if (s == "getpopular") return Workload::POPULAR_GET;
    if (s == "mixed") return Workload::MIXED;

    cerr << "Invalid workload: " << s << endl;
    exit(1);
}

string make_key(int k) { return "key" + to_string(k); }
string make_value(int v) { return "value" + to_string(v); }

void perclientfunction(
    int id,
    Workload workload,
    int duration_sec,
    atomic<long long> &total_req,
    atomic<long long> &total_latency_ns,
    const string &host,
    int port
) {
    httplib::Client cli(host, port);
    cli.set_keep_alive(true);

    auto stop_time = steady_clock::now() + seconds(duration_sec);

    long long local_req = 0;
    long long local_lat = 0;

    mt19937 rng(id + 12345);
    uniform_int_distribution<int> key_dist(1, 50000);
    uniform_int_distribution<int> popular_dist(1, 5);
    uniform_int_distribution<int> mix_dist(0, 2);

    while (steady_clock::now() < stop_time) {
        int k = key_dist(rng);
        string key = make_key(k);
        string value = make_value(k);

        auto start = steady_clock::now();
        bool ok = false;

        switch (workload) {
            case Workload::PUT_ALL: {
                string body = "key=" + key + "&value=" + value;
                auto res = cli.Post("/create", body, "application/x-www-form-urlencoded");
                ok = res && res->status == 200;
                break;
            }
            case Workload::GET_ALL: {
                auto res = cli.Get(("/read?key=" + key).c_str());
                ok = res && res->status == 200;
                break;
            }
            case Workload::POPULAR_GET: {
                int pk = popular_dist(rng);
                string pop_key = make_key(pk);
                auto res = cli.Get(("/read?key=" + pop_key).c_str());
                ok = res && res->status == 200;
                break;
            }
            case Workload::MIXED: {
                int r = mix_dist(rng);
                if (r == 0) {
                    string body = "key=" + key + "&value=" + value;
                    auto res = cli.Post("/create", body, "application/x-www-form-urlencoded");
                    ok = res && res->status == 200;
                } else if (r == 1) {
                    auto res = cli.Get(("/read?key=" + key).c_str());
                    ok = res && res->status == 200;
                } else {
                    auto res = cli.Delete(("/delete?key=" + key).c_str());
                    ok = res && res->status == 200;
                }
                break;
            }
        }

        auto end = steady_clock::now();
        long long ns = duration_cast<nanoseconds>(end - start).count();

        if (ok) {
            local_req++;
            local_lat += ns;
        }
    }

    total_req += local_req;
    total_latency_ns += local_lat;
}

string get_arg(int argc, char *argv[], const string &flag, const string &def = "") {
    for (int i = 1; i < argc - 1; i++) {
        if (string(argv[i]) == flag)
            return string(argv[i+1]);
    }
    return def;
}

int main(int argc, char *argv[]) {
    if (argc < 9) {
        cout << "Usage:\n"
             << "  ./loadgen --workload getpopular --threads 100 "
             << "--duration 300 --host localhost --port 8080\n";
        return 1;
    }

    string workload_str = get_arg(argc, argv, "--workload");
    string threads_str  = get_arg(argc, argv, "--threads");
    string dur_str      = get_arg(argc, argv, "--duration");
    string host         = get_arg(argc, argv, "--host", "localhost");
    string port_str     = get_arg(argc, argv, "--port", "8080");

    if (workload_str.empty() || threads_str.empty() || dur_str.empty()) {
        cerr << "Missing required arguments.\n";
        return 1;
    }

    Workload workload = parse_workload(workload_str);
    int threads = stoi(threads_str);
    int duration = stoi(dur_str);
    int port = stoi(port_str);

    cout << "Loadgen starting...\n";
    cout << "Workload: " << workload_str << "\nThreads: " << threads
         << "\nDuration: " << duration << " sec\nHost: " << host
         << "\nPort: " << port << "\n";

    atomic<long long> total_req(0);
    atomic<long long> total_lat_ns(0);

    vector<thread> pool;
    auto start = steady_clock::now();

    for (int i = 0; i < threads; i++) {
        pool.emplace_back(perclientfunction, i, workload, duration,
                          ref(total_req), ref(total_lat_ns), host, port);
    }

    for (auto &t : pool) t.join();

    auto end = steady_clock::now();
    double secs = duration_cast<seconds>(end - start).count();

    long long req = total_req.load();
    long long lat = total_lat_ns.load();

    cout << "Total Requests: " << req << "\n";
    cout << "Throughput: " << (req / secs) << " req/s\n";
    cout << "Avg Latency: " << (lat / 1e6) / req << " ms\n";
    cout << "===================\n";

    return 0;
}
