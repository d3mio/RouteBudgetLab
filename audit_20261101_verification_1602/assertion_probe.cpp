#include <iostream>
#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "FAILED [" << __FILE__ << ":" << __LINE__ << "]: " << msg \
                  << " (" << #cond << " is false)" << std::endl; \
        return false; \
    } \
} while(0)

bool probe(){ ASSERT_TRUE(false,"intentional audit assertion failure"); return true; }
int main(){return probe()?0:17;}
