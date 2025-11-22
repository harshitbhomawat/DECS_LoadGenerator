#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <mutex>
#include "httplib.h"

using namespace std;
using namespace chrono;

enum Workload {
    PUT_ALL,
    GET_ALL,
    POPULAR_GET,
    MIXED
};

string generate_random_value(int len = 10) {
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    static thread_local mt19937 rng(random_device{}());
    uniform_int_distribution<int> dist(0, sizeof(chars) - 2);

    string s;
    for (int i = 0; i < len; i++) s.push_back(chars[dist(rng)]);
    return s;
}

void client_thread_func(
    int id,
    Workload workload,
    int duration_sec,
    atomic<long long> &total_requests,
    atomic<long long> &total_latency_ns,
    const string &server_host,
    int server_port
) {
    httplib::Client cli(server_host, server_port);
    cli.set_keep_alive(true);

    auto end_time = steady_clock::now() + seconds(duration_sec);

    long long local_count = 0;
    long long local_latency = 0;

    static vector<string> popular_keys = {"k1", "k2", "k3", "k4", "k5"};

    mt19937 rng(id + time(NULL));
    uniform_int_distribution<int> key_dist(1, 500000);
    uniform_int_distribution<int> pop_dist(0, popular_keys.size() - 1);
    uniform_int_distribution<int> mix_dist(0, 2);

    while (steady_clock::now() < end_time) {
        string key = "k" + to_string(key_dist(rng));
        string value = generate_random_value(12);

        bool success = false;

        auto start = steady_clock::now();

        switch (workload) {
            case PUT_ALL: {
                string body = "key=" + key + "&value=" + value;
                auto res = cli.Post("/create", body, "application/x-www-form-urlencoded");
                success = (res && res->status == 200);
                break;
            }

            case GET_ALL: {
                auto res = cli.Get(("/read?key=" + key).c_str());
                success = (res && res->status == 200);
                break;
            }

            case POPULAR_GET: {
                string hot_key = popular_keys[pop_dist(rng)];
                auto res = cli.Get(("/read?key=" + hot_key).c_str());
                success = (res && res->status == 200);
                break;
            }

            case MIXED: {
                int r = mix_dist(rng);

                if (r == 0) {  // PUT
                    string body = "key=" + key + "&value=" + value;
                    auto res = cli.Post("/create", body, "application/x-www-form-urlencoded");
                    success = (res && res->status == 200);
                } else if (r == 1) {  // GET
                    auto res = cli.Get(("/read?key=" + key).c_str());
                    success = (res && res->status == 200);
                } else {  // DELETE
                    auto res = cli.Delete(("/delete?key=" + key).c_str());
                    success = (res && res->status == 200);
                }
                break;
            }
        }

        auto end = steady_clock::now();
        auto latency_ns = duration_cast<nanoseconds>(end - start).count();

        if (success) {
            local_count++;
            local_latency += latency_ns;
        }
    }

    total_requests += local_count;
    total_latency_ns += local_latency;
}

Workload parse_workload(const string &s) {
    if (s == "putall") return PUT_ALL;
    if (s == "getall") return GET_ALL;
    if (s == "popular") return POPULAR_GET;
    if (s == "mixed") return MIXED;

    cerr << "Unknown workload: " << s << endl;
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        cout << "Usage: " << argv[0] <<
            " <num_threads> <duration_sec> <workload> <server_host> [port=8080]\n";
        cout << "Workloads: putall | getall | popular | mixed\n";
        return 1;
    }

    int num_threads = stoi(argv[1]);
    int duration_sec = stoi(argv[2]);
    Workload workload = parse_workload(argv[3]);
    string server_host = argv[4];
    int server_port = (argc >= 6 ? stoi(argv[5]) : 8080);

    cout << "Load Generator Starting...\n";
    cout << "Threads: " << num_threads << "\n";
    cout << "Duration: " << duration_sec << " seconds\n";
    cout << "Workload: " << argv[3] << "\n";
    cout << "Server:   " << server_host << ":" << server_port << "\n";

    atomic<long long> total_requests(0);
    atomic<long long> total_latency_ns(0);

    vector<thread> threads;

    auto start_time = steady_clock::now();

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(client_thread_func,
                             i, workload, duration_sec,
                             ref(total_requests), ref(total_latency_ns),
                             ref(server_host), server_port);
    }

    for (auto &t : threads) t.join();

    auto end_time = steady_clock::now();
    double runtime = duration_cast<seconds>(end_time - start_time).count();

    long long req = total_requests.load();
    long long latency_ns = total_latency_ns.load();

    int throughput = req / runtime;
    double avg_latency_ms = (latency_ns / 1e6) / req;

    cout << "\n===== LOAD TEST RESULTS =====\n";
    cout << "Total Requests: " << req << "\n";
    cout << "Throughput (req/sec): " << throughput << "\n";
    cout << "Average Latency: " << avg_latency_ms << " ms\n";
    cout << "=============================\n";

    return 0;
}
