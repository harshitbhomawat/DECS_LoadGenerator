# DECS_LoadGenerator
Load Generator for the Key-Value server made using C++, it will help in finding bottlenecks in the server.


//To Compile:
g++ loadgen.cpp -o loadgen -pthread -std=c++17

//PutAll
./loadgen 20 15 putall localhost


//GetAll
./loadgen 20 10 getall localhost

//Popular
./loadgen 20 10 popular localhost

//Mixed
./loadgen 50 20 mixed localhost

