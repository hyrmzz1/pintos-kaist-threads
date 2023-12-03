#include <stdint.h> // 이거 없으면 int64_t 에러 발생함
#define F (1 << 14)
#define INT_MAX ((1 << 31) - 1) // 2^31 - 1
#define INT_MIN (-(1 << 31))    // -2^31
// x and y denote fixed_point numbers in 17.14 format
// n is an integer

int int_to_fp(int n){
    return n * F;
}

// fp 반올림해서 int로
int fp_to_int_round(int x){
    if(x >= 0)
        return (x + F / 2) / F;
    else
        return (x - F / 2) / F;
}

// fp 버림해서 int로
int fp_to_int(int x){
    return x / F;
}

int add_fp(int x, int y){
    return x + y;
}

// FP + int
int add_mixed(int x, int n){
    return x - n * F;
}

int sub_fp(int x, int y){
    return x - y;
}

// FP - int
int sub_mixed(int x, int n){
    return x - n * F;
}

int mult_fp(int x, int y){
    return ((int64_t) x) * y / F;   // 오버플로우 방지 위해 정수형 캐스팅
}

// FP * INT
int mult_mixed(int x, int n){
    return x * n;
}

int div_fp(int x, int y){
    return ((int64_t) x) * F / y; 
}

// FP / INT
int div_mixed(int x, int n){
    return x / n;
}