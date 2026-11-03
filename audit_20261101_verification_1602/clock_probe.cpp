#include <mach/mach_time.h>
#include <chrono>
#include <iostream>
#include <limits>
int main(){mach_timebase_info_data_t t;mach_timebase_info(&t);std::cout<<"mach_timebase numerator="<<t.numer<<" denominator="<<t.denom<<" tick_ns="<<double(t.numer)/t.denom<<"\n";long long minpos=std::numeric_limits<long long>::max();int zeros=0;for(int i=0;i<100000;i++){auto a=std::chrono::steady_clock::now();auto b=std::chrono::steady_clock::now();auto d=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();if(d==0)zeros++;else if(d<minpos)minpos=d;}std::cout<<"empty clock pairs=100000 zero_differences="<<zeros<<" minimum_positive_ns="<<minpos<<"\n";}
